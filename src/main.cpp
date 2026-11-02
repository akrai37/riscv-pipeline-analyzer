#include <cstdio>
#include <cstring>

namespace {

void print_usage(const char* argv0) {
  std::printf(
      "usage: %s [--version] [--help]\n"
      "\n"
      "Cycle-accurate 5-stage RV32IM pipeline simulator.\n",
      argv0);
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--version") == 0) {
      std::printf("rvsim %s\n", RVSIM_VERSION);
      return 0;
    }
    if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    std::fprintf(stderr, "rvsim: unknown argument '%s'\n", argv[i]);
    print_usage(argv[0]);
    return 2;
  }
  print_usage(argv[0]);
  return 2;
}
