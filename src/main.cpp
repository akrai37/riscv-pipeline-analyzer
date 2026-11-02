#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>

#include "rvsim/elf.hpp"
#include "rvsim/error.hpp"
#include "rvsim/lockstep.hpp"
#include "rvsim/memory.hpp"
#include "rvsim/pipeline.hpp"
#include "rvsim/platform.hpp"
#include "rvsim/reference.hpp"

namespace {

// Process exit status of rvsim itself; the program's own exit code is
// reported in the summary line.
constexpr int kExitPass = 0;
constexpr int kExitProgramFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitSimError = 3;
constexpr int kExitLimit = 4;
constexpr int kExitDiverged = 5;

struct Options {
  std::string program;
  std::string model = "pipeline";
  uint64_t limit = 2'000'000'000;  // instructions (ref) or cycles (pipeline)
  bool lockstep = false;
  bool quiet = false;
  bool stats = false;
  bool trace = false;
  rvsim::PipelineConfig pipeline;
};

void print_usage(std::FILE* out) {
  std::fprintf(out,
               "usage: rvsim [options] <program.elf>\n"
               "\n"
               "Runs a static RV32IM ELF executable on the 5-stage pipeline model or the\n"
               "sequential reference model.\n"
               "\n"
               "options:\n"
               "  --model pipeline|ref    execution model (default: pipeline)\n"
               "  --lockstep              check the pipeline against the reference model\n"
               "                          instruction by instruction\n"
               "  --no-forwarding         disable EX/MEM and MEM/WB forwarding\n"
               "  --predictor P           not-taken | btfn | 2bit (default: 2bit)\n"
               "  --btb N                 BTB entries, power of two, 0 = none (default: 64)\n"
               "  --pht N                 2-bit counters, power of two (default: 256)\n"
               "  --ras N                 return address stack entries, 0-16 (default: 8)\n"
               "  --mul-latency N         multiply latency in cycles (default: 3)\n"
               "  --div-latency N         divide latency in cycles (default: 32)\n"
               "  --limit N               stop after N cycles (pipeline) or instructions (ref)\n"
               "  --stats                 print the CPI breakdown and branch statistics\n"
               "  --trace                 print one line per cycle to stderr\n"
               "  -q, --quiet             do not print the summary line\n"
               "  --version               print the version and exit\n"
               "  -h, --help              print this help and exit\n"
               "\n"
               "exit status: 0 program exited 0, 1 program exited non-zero, 2 usage error,\n"
               "             3 simulation error, 4 limit reached, 5 lockstep divergence\n");
}

[[noreturn]] void usage_error(const std::string& msg) {
  std::fprintf(stderr, "rvsim: %s\n", msg.c_str());
  print_usage(stderr);
  std::exit(kExitUsage);
}

uint64_t parse_uint(std::string_view flag, const std::string& v) {
  char* end = nullptr;
  const unsigned long long n = std::strtoull(v.c_str(), &end, 10);
  if (v.empty() || *end != '\0' || v[0] == '-') {
    usage_error(std::string(flag) + " needs a non-negative integer");
  }
  return n;
}

unsigned parse_small(std::string_view flag, const std::string& v) {
  const uint64_t n = parse_uint(flag, v);
  if (n > 1'000'000) {
    usage_error(std::string(flag) + " is out of range");
  }
  return static_cast<unsigned>(n);
}

Options parse(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argc) {
        usage_error(std::string(arg) + " needs a value");
      }
      return argv[++i];
    };
    rvsim::PredictorConfig& pred = opt.pipeline.predictor;
    if (arg == "--version") {
      std::printf("rvsim %s\n", RVSIM_VERSION);
      std::exit(kExitPass);
    } else if (arg == "-h" || arg == "--help") {
      print_usage(stdout);
      std::exit(kExitPass);
    } else if (arg == "-q" || arg == "--quiet") {
      opt.quiet = true;
    } else if (arg == "--stats") {
      opt.stats = true;
    } else if (arg == "--trace") {
      opt.trace = true;
    } else if (arg == "--lockstep") {
      opt.lockstep = true;
    } else if (arg == "--model") {
      opt.model = value();
    } else if (arg == "--no-forwarding") {
      opt.pipeline.forwarding = false;
    } else if (arg == "--predictor") {
      const std::string p = value();
      if (p == "not-taken") {
        pred.direction = rvsim::DirectionPredictor::NotTaken;
      } else if (p == "btfn") {
        pred.direction = rvsim::DirectionPredictor::Btfn;
      } else if (p == "2bit") {
        pred.direction = rvsim::DirectionPredictor::TwoBit;
      } else {
        usage_error("unknown predictor '" + p + "'");
      }
    } else if (arg == "--btb") {
      pred.btb_entries = parse_small(arg, value());
    } else if (arg == "--pht") {
      pred.pht_entries = parse_small(arg, value());
    } else if (arg == "--ras") {
      pred.ras_entries = parse_small(arg, value());
    } else if (arg == "--mul-latency") {
      opt.pipeline.mul_latency = parse_small(arg, value());
    } else if (arg == "--div-latency") {
      opt.pipeline.div_latency = parse_small(arg, value());
    } else if (arg == "--limit") {
      opt.limit = parse_uint(arg, value());
    } else if (!arg.empty() && arg[0] == '-') {
      usage_error("unknown option '" + std::string(arg) + "'");
    } else if (opt.program.empty()) {
      opt.program = arg;
    } else {
      usage_error("only one program may be given");
    }
  }
  if (opt.program.empty()) {
    usage_error("no program given");
  }
  if (opt.model != "pipeline" && opt.model != "ref") {
    usage_error("unknown model '" + opt.model + "'");
  }
  if (opt.lockstep && opt.model != "pipeline") {
    usage_error("--lockstep runs the pipeline model");
  }
  return opt;
}

double percent(uint64_t part, uint64_t whole) {
  return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

void print_stats(const rvsim::Pipeline& pipe) {
  const rvsim::PipelineStats& s = pipe.stats();
  const rvsim::PipelineConfig& c = pipe.config();
  std::fprintf(stderr, "config: forwarding=%s predictor=%s btb=%u pht=%u ras=%u mul=%u div=%u\n",
               c.forwarding ? "on" : "off", rvsim::direction_name(c.predictor.direction),
               c.predictor.btb_entries, c.predictor.pht_entries, c.predictor.ras_entries,
               c.mul_latency, c.div_latency);
  std::fprintf(stderr, "cycles        %12llu\n", static_cast<unsigned long long>(s.cycles));
  std::fprintf(stderr, "instructions  %12llu\n", static_cast<unsigned long long>(s.retired));
  std::fprintf(stderr, "CPI           %12.4f\n", s.cpi());
  std::fprintf(stderr, "lost cycles   %12llu  (cycles = instructions + lost)\n",
               static_cast<unsigned long long>(s.total_lost()));
  for (std::size_t i = 0; i < rvsim::kStallCauses; ++i) {
    const auto cause = static_cast<rvsim::StallCause>(i);
    if (s.lost[i] != 0) {
      std::fprintf(stderr, "  %-18s %10llu  %5.2f%% of cycles  CPI +%.4f\n",
                   rvsim::stall_name(cause), static_cast<unsigned long long>(s.lost[i]),
                   percent(s.lost[i], s.cycles),
                   s.retired == 0 ? 0.0 : static_cast<double>(s.lost[i]) / s.retired);
    }
  }
  const uint64_t control = s.branches + s.jals + s.returns + s.other_jalrs;
  std::fprintf(stderr, "branches      %12llu  taken %.1f%%  mispredicted %.2f%%\n",
               static_cast<unsigned long long>(s.branches), percent(s.branches_taken, s.branches),
               percent(s.branch_mispredicts, s.branches));
  std::fprintf(stderr, "jal           %12llu  BTB misses %llu\n",
               static_cast<unsigned long long>(s.jals),
               static_cast<unsigned long long>(s.jal_misses));
  std::fprintf(stderr, "returns       %12llu  mispredicted %llu\n",
               static_cast<unsigned long long>(s.returns),
               static_cast<unsigned long long>(s.return_mispredicts));
  std::fprintf(stderr, "other jalr    %12llu  mispredicted %llu\n",
               static_cast<unsigned long long>(s.other_jalrs),
               static_cast<unsigned long long>(s.other_jalr_mispredicts));
  std::fprintf(stderr, "BTB hit rate on control flow  %.2f%%\n",
               percent(s.control_btb_hits, control));
  std::fprintf(stderr, "loads %llu  stores %llu  mul %llu  div %llu\n",
               static_cast<unsigned long long>(s.loads), static_cast<unsigned long long>(s.stores),
               static_cast<unsigned long long>(s.muls), static_cast<unsigned long long>(s.divs));
}

void write_output(const rvsim::Host& host) {
  std::fwrite(host.output().data(), 1, host.output().size(), stdout);
  std::fflush(stdout);
}

int finish(const Options& opt, bool halted, int exit_code, uint64_t instructions, uint64_t cycles) {
  if (!halted) {
    if (!opt.quiet) {
      std::fprintf(stderr, "[rvsim] limit of %llu reached\n",
                   static_cast<unsigned long long>(opt.limit));
    }
    return kExitLimit;
  }
  if (!opt.quiet) {
    if (opt.model == "pipeline") {
      std::fprintf(stderr,
                   "[rvsim] model=pipeline exit=%d instructions=%llu cycles=%llu CPI=%.4f\n",
                   exit_code, static_cast<unsigned long long>(instructions),
                   static_cast<unsigned long long>(cycles),
                   static_cast<double>(cycles) / static_cast<double>(instructions));
    } else {
      std::fprintf(stderr, "[rvsim] model=ref exit=%d instructions=%llu\n", exit_code,
                   static_cast<unsigned long long>(instructions));
    }
  }
  return exit_code == 0 ? kExitPass : kExitProgramFailed;
}

int run_reference(const Options& opt, const std::vector<uint8_t>& image) {
  rvsim::Memory mem(rvsim::kRamBase, rvsim::kRamSize);
  rvsim::ReferenceModel ref(mem, rvsim::load_elf(image, mem));
  try {
    ref.run(opt.limit);
  } catch (const rvsim::SimError& e) {
    write_output(ref.host());
    std::fprintf(stderr, "rvsim: %s (after %llu instructions)\n", e.what(),
                 static_cast<unsigned long long>(ref.retired()));
    return kExitSimError;
  }
  write_output(ref.host());
  return finish(opt, ref.halted(), ref.exit_code(), ref.retired(), 0);
}

int run_pipeline(const Options& opt, const std::vector<uint8_t>& image) {
  rvsim::Memory mem(rvsim::kRamBase, rvsim::kRamSize);
  rvsim::Pipeline pipe(mem, rvsim::load_elf(image, mem), opt.pipeline);
  if (opt.trace) {
    pipe.set_trace(stderr);
  }

  if (opt.lockstep) {
    rvsim::Memory ref_mem(rvsim::kRamBase, rvsim::kRamSize);
    rvsim::ReferenceModel ref(ref_mem, rvsim::load_elf(image, ref_mem));
    const rvsim::LockstepResult r = rvsim::run_lockstep(pipe, ref, opt.limit);
    write_output(pipe.host());
    if (opt.stats) {
      print_stats(pipe);
    }
    if (!opt.quiet || !r.ok()) {
      std::fprintf(stderr, "[rvsim] lockstep %s: %llu instructions matched%s%s\n",
                   rvsim::outcome_name(r.outcome), static_cast<unsigned long long>(r.compared),
                   r.message.empty() ? "" : "\n  ", r.message.c_str());
    }
    switch (r.outcome) {
      case rvsim::LockstepResult::Outcome::Diverged:
        return kExitDiverged;
      case rvsim::LockstepResult::Outcome::Faulted:
        return kExitSimError;
      case rvsim::LockstepResult::Outcome::Limit:
      case rvsim::LockstepResult::Outcome::Exited:
        break;
    }
    return finish(opt, pipe.halted(), pipe.exit_code(), pipe.stats().retired, pipe.stats().cycles);
  }

  try {
    pipe.run(opt.limit);
  } catch (const rvsim::SimError& e) {
    write_output(pipe.host());
    std::fprintf(stderr, "rvsim: %s (cycle %llu)\n", e.what(),
                 static_cast<unsigned long long>(pipe.stats().cycles));
    return kExitSimError;
  }
  write_output(pipe.host());
  if (opt.stats) {
    print_stats(pipe);
  }
  return finish(opt, pipe.halted(), pipe.exit_code(), pipe.stats().retired, pipe.stats().cycles);
}

}  // namespace

int main(int argc, char** argv) {
  const Options opt = parse(argc, argv);
  try {
    const std::vector<uint8_t> image = rvsim::read_file(opt.program);
    return opt.model == "ref" ? run_reference(opt, image) : run_pipeline(opt, image);
  } catch (const rvsim::LoadError& e) {
    std::fprintf(stderr, "rvsim: %s: %s\n", opt.program.c_str(), e.what());
    return kExitUsage;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "rvsim: %s\n", e.what());
    return kExitUsage;
  }
}
