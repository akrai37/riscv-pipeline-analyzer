include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(rvsim_tests
  decoder_test.cpp
  elf_test.cpp
  exec_test.cpp
  memory_test.cpp
  program_test.cpp
  reference_test.cpp)
target_link_libraries(rvsim_tests PRIVATE rvsim_core GTest::gtest_main)
target_compile_options(rvsim_tests PRIVATE -Wall -Wextra)
target_compile_definitions(rvsim_tests PRIVATE
  RVSIM_WORKLOAD_DIR="${PROJECT_SOURCE_DIR}/workloads/bin"
  RVSIM_RISCV_TESTS_DIR="${PROJECT_SOURCE_DIR}/tests/riscv-tests/bin")

include(GoogleTest)
gtest_discover_tests(rvsim_tests)
