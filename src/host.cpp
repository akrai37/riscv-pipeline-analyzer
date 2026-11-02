#include "rvsim/host.hpp"

#include <format>

#include "rvsim/error.hpp"

namespace rvsim {
namespace {

constexpr unsigned kA0 = 10;
constexpr unsigned kA1 = 11;
constexpr unsigned kA2 = 12;
constexpr unsigned kA7 = 17;

constexpr uint32_t kSysWrite = 64;
constexpr uint32_t kSysExit = 93;

}  // namespace

uint32_t Host::ecall(const RegFile& regs, const Memory& mem) {
  switch (regs[kA7]) {
    case kSysWrite: {
      const uint32_t fd = regs[kA0];
      const uint32_t buf = regs[kA1];
      const uint32_t len = regs[kA2];
      if (fd != 1 && fd != 2) {
        throw SimError(std::format("write: unsupported fd {}", fd));
      }
      if (len != 0 && !mem.contains(buf, len)) {
        throw SimError(std::format("write: buffer {:#010x}+{} outside memory", buf, len));
      }
      for (uint32_t i = 0; i < len; ++i) {
        output_.push_back(static_cast<char>(mem.load(buf + i, 1)));
      }
      return len;
    }
    case kSysExit:
      exited_ = true;
      exit_code_ = static_cast<int>(regs[kA0]);
      return regs[kA0];
    default:
      throw SimError(std::format("unsupported syscall {} (a7)", regs[kA7]));
  }
}

}  // namespace rvsim
