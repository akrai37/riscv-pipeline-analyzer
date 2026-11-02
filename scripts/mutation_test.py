#!/usr/bin/env python3
"""Checks that the verification actually catches pipeline bugs.

Each mutation plants one realistic bug in a scratch copy of the repository,
rebuilds, and records how many fuzz runs diverge and how many program
lockstep and directed timing tests fail. A mutation no layer catches is a
hole in the verification.

    scripts/mutation_test.py [--seeds N]

Needs cmake and a C++20 compiler; GoogleTest is reused from ./build if it has
already been fetched there.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (description, file, original text, mutated text)
MUTATIONS = [
    (
        "bypass priority swapped (older MEM/WB value wins)",
        "src/pipeline.cpp",
        "  if (ex_mem_.valid && ex_mem_.writes && ex_mem_.rd == reg) {\n    // A load",
        "  if (mem_wb_.valid && mem_wb_.writes && mem_wb_.rd == reg) {\n"
        "    ++stats_.bypass_mem_wb;\n    return mem_wb_.result;\n  }\n"
        "  if (ex_mem_.valid && ex_mem_.writes && ex_mem_.rd == reg) {\n    // A load",
    ),
    (
        "store base register exempt from the load-use stall",
        "src/pipeline.cpp",
        "depends_on(id_ex_, !is_store(in.op))",
        "(!is_store(in.op) && depends_on(id_ex_, true))",
    ),
    (
        "no MEM/WB -> MEM store-data forwarding",
        "src/pipeline.cpp",
        "if (config_.forwarding && s.in.rs2 != 0 && mem_wb_.valid",
        "if (false && s.in.rs2 != 0 && mem_wb_.valid",
    ),
    (
        "ecall waits for EX but not MEM to drain",
        "src/pipeline.cpp",
        "if (id_ex_.valid || ex_mem_.valid) {\n      return StallCause::Serialize;",
        "if (id_ex_.valid) {\n      return StallCause::Serialize;",
    ),
    (
        "instruction in ID not squashed on a flush",
        "src/pipeline.cpp",
        "    next_id_ex = bubble(redirect->cause);\n    next_if_id = bubble(redirect->cause);",
        "    next_id_ex = if_id_;\n    next_if_id = bubble(redirect->cause);",
    ),
    (
        "forwarding off: hazard check ignores the MEM-stage producer",
        "src/pipeline.cpp",
        "if (depends_on(id_ex_, true) || depends_on(ex_mem_, true)) {",
        "if (depends_on(id_ex_, true)) {",
    ),
    (
        "x0 treated as a real destination",
        "src/pipeline.cpp",
        "s.writes = s.rd != 0;",
        "s.writes = writes_rd(s.in.op) || s.rd != 0;",
    ),
]


def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def failed_tests(binary, gtest_filter, cwd):
    out = run([binary, "--gtest_brief=1", f"--gtest_filter={gtest_filter}"], cwd).stdout
    return len(re.findall(r"^\[  FAILED  \] \S+\.\S+", out, re.M))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--seeds", type=int, default=2000)
    args = parser.parse_args()

    files = run(["git", "ls-files", "-co", "--exclude-standard"], ROOT).stdout.split()
    gtest = os.path.join(ROOT, "build", "_deps", "googletest-src")
    configure_extra = [f"-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST={gtest}"] if os.path.isdir(gtest) else []

    print(f"| Mutation | Fuzz runs diverged (of {2 * args.seeds}) | Program lockstep failing | "
          "Directed tests failing |")
    print("|---|---:|---:|---:|")
    holes = 0
    for name, path, before, after in MUTATIONS:
        with tempfile.TemporaryDirectory() as tmp:
            for f in files:
                if f.startswith("build"):
                    continue
                os.makedirs(os.path.join(tmp, os.path.dirname(f)), exist_ok=True)
                shutil.copy2(os.path.join(ROOT, f), os.path.join(tmp, f))
            target = os.path.join(tmp, path)
            text = open(target).read()
            if text.count(before) != 1:
                sys.exit(f"mutation '{name}' no longer applies to {path}")
            open(target, "w").write(text.replace(before, after))

            build = os.path.join(tmp, "b")
            if run(["cmake", "-S", tmp, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
                    *configure_extra], tmp).returncode != 0 or \
                    run(["cmake", "--build", build, "-j"], tmp).returncode != 0:
                sys.exit(f"mutation '{name}' does not build")

            fuzz = run([os.path.join(build, "rvsim-fuzz"), "--seeds", str(args.seeds)], tmp).stdout
            diverged = int(re.search(r"(\d+) diverged$", fuzz, re.M).group(1))
            tests = os.path.join(build, "tests", "rvsim_tests")
            programs = failed_tests(tests, "*PipelineLockstep*", tmp)
            directed = failed_tests(
                tests, "PipelineTiming*:PipelineBranches*:PipelineFaults*:PipelineSyscalls*", tmp)
            holes += diverged == 0 and programs == 0 and directed == 0
            print(f"| {name} | {diverged} | {programs} | {directed} |", flush=True)
    return 1 if holes else 0


if __name__ == "__main__":
    sys.exit(main())
