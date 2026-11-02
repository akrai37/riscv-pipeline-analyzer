#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>

#include "rvsim/elf.hpp"
#include "rvsim/error.hpp"
#include "rvsim/memory.hpp"
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

struct Options {
  std::string program;
  std::string model = "ref";
  uint64_t max_instructions = 2'000'000'000;
  bool quiet = false;
};

void print_usage(std::FILE* out) {
  std::fprintf(out,
               "usage: rvsim [options] <program.elf>\n"
               "\n"
               "Runs a static RV32IM ELF executable.\n"
               "\n"
               "options:\n"
               "  --model ref             execution model (default: ref)\n"
               "  --max-instructions N    stop after N instructions (default: 2000000000)\n"
               "  -q, --quiet             do not print the summary line\n"
               "  --version               print the version and exit\n"
               "  -h, --help              print this help and exit\n"
               "\n"
               "exit status: 0 program exited 0, 1 program exited non-zero, 2 usage error,\n"
               "             3 simulation error, 4 instruction limit reached\n");
}

[[noreturn]] void usage_error(const std::string& msg) {
  std::fprintf(stderr, "rvsim: %s\n", msg.c_str());
  print_usage(stderr);
  std::exit(kExitUsage);
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
    if (arg == "--version") {
      std::printf("rvsim %s\n", RVSIM_VERSION);
      std::exit(kExitPass);
    } else if (arg == "-h" || arg == "--help") {
      print_usage(stdout);
      std::exit(kExitPass);
    } else if (arg == "-q" || arg == "--quiet") {
      opt.quiet = true;
    } else if (arg == "--model") {
      opt.model = value();
    } else if (arg == "--max-instructions") {
      const std::string v = value();
      char* end = nullptr;
      opt.max_instructions = std::strtoull(v.c_str(), &end, 10);
      if (v.empty() || *end != '\0' || opt.max_instructions == 0) {
        usage_error("--max-instructions needs a positive integer");
      }
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
  if (opt.model != "ref") {
    usage_error("unknown model '" + opt.model + "'");
  }
  return opt;
}

}  // namespace

int main(int argc, char** argv) {
  const Options opt = parse(argc, argv);
  try {
    rvsim::Memory mem(rvsim::kRamBase, rvsim::kRamSize);
    const uint32_t entry = rvsim::load_elf(rvsim::read_file(opt.program), mem);
    rvsim::ReferenceModel ref(mem, entry);

    int status = kExitPass;
    try {
      ref.run(opt.max_instructions);
    } catch (const rvsim::SimError& e) {
      std::fwrite(ref.host().output().data(), 1, ref.host().output().size(), stdout);
      std::fprintf(stderr, "rvsim: %s (after %llu instructions)\n", e.what(),
                   static_cast<unsigned long long>(ref.retired()));
      return kExitSimError;
    }
    std::fwrite(ref.host().output().data(), 1, ref.host().output().size(), stdout);
    std::fflush(stdout);

    if (!ref.halted()) {
      status = kExitLimit;
      if (!opt.quiet) {
        std::fprintf(stderr, "[rvsim] instruction limit reached after %llu instructions\n",
                     static_cast<unsigned long long>(ref.retired()));
      }
      return status;
    }
    if (ref.exit_code() != 0) {
      status = kExitProgramFailed;
    }
    if (!opt.quiet) {
      std::fprintf(stderr, "[rvsim] model=%s exit=%d instructions=%llu\n", opt.model.c_str(),
                   ref.exit_code(), static_cast<unsigned long long>(ref.retired()));
    }
    return status;
  } catch (const rvsim::LoadError& e) {
    std::fprintf(stderr, "rvsim: %s: %s\n", opt.program.c_str(), e.what());
    return kExitUsage;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "rvsim: %s\n", e.what());
    return kExitSimError;
  }
}
