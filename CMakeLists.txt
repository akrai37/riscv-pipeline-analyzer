cmake_minimum_required(VERSION 3.20)
project(riscv-pipeline-analyzer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(RVSIM_WERROR "Treat compiler warnings as errors" OFF)
option(RVSIM_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(RVSIM_BUILD_TESTS "Build the unit and integration tests" ON)

if(RVSIM_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

# Strict warnings apply to simulator code only, not to third-party code.
add_library(rvsim_warnings INTERFACE)
target_compile_options(rvsim_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wold-style-cast -Wnon-virtual-dtor -Wimplicit-fallthrough)
if(RVSIM_WERROR)
  target_compile_options(rvsim_warnings INTERFACE -Werror)
endif()

add_library(rvsim_core
  src/isa.cpp
  src/elf.cpp
  src/exec.cpp
  src/fuzz.cpp
  src/memory.cpp
  src/host.cpp
  src/lockstep.cpp
  src/pipeline.cpp
  src/predictor.cpp
  src/reference.cpp
  src/report.cpp)
target_include_directories(rvsim_core PUBLIC include)
target_link_libraries(rvsim_core PRIVATE rvsim_warnings)

add_executable(rvsim src/main.cpp)
target_link_libraries(rvsim PRIVATE rvsim_core rvsim_warnings)
target_compile_definitions(rvsim PRIVATE RVSIM_VERSION="${PROJECT_VERSION}")

add_executable(rvsim-fuzz tools/fuzz_main.cpp)
target_link_libraries(rvsim-fuzz PRIVATE rvsim_core rvsim_warnings)

if(RVSIM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
