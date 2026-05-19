# cmake/run_bench_regression.cmake
# CTest script for running a Google Benchmark binary and comparing the results
# against a committed baseline JSON via tools/bench_compare.py.
#
# Invoked by bench_threading_regression (T056 / [const §VIII.2]) and any
# future bench_*_regression CTest entry that follows the same pattern.
#
# Variables expected by the caller (passed via -D):
#   BENCH_BINARY    — path to the compiled benchmark binary
#   BASELINE_JSON   — path to the committed baseline JSON
#   CURRENT_JSON    — output path for the current run's JSON
#   TOOLS_DIR       — directory containing bench_compare.py
#
# Exit codes:
#   0  — regression check passed (or baseline missing → skip)
#   1  — benchmark binary execution failed
#  (bench_compare.py itself always exits 0 per [const §VIII.2] SOFT gate)
#
# The gate is SOFT: bench_compare.py prints a warning on >±5% regression but
# always exits 0. This cmake script propagates that exit code unchanged.

cmake_minimum_required(VERSION 3.20)

if(NOT EXISTS "${BENCH_BINARY}")
  message(STATUS "[bench_regression] Binary not found: ${BENCH_BINARY} — skipping")
  return()
endif()

if(NOT EXISTS "${BASELINE_JSON}")
  message(STATUS "[bench_regression] Baseline not found: ${BASELINE_JSON} — skipping (run once to capture)")
  return()
endif()

# Create output directory.
get_filename_component(_out_dir "${CURRENT_JSON}" DIRECTORY)
file(MAKE_DIRECTORY "${_out_dir}")

# Run the benchmark binary and capture JSON output.
message(STATUS "[bench_regression] Running ${BENCH_BINARY} ...")
execute_process(
  COMMAND "${BENCH_BINARY}"
          --benchmark_format=json
          "--benchmark_out=${CURRENT_JSON}"
          --benchmark_repetitions=3
          --benchmark_min_time=0.1s
  RESULT_VARIABLE _bench_rc
)

if(NOT _bench_rc EQUAL 0)
  message(FATAL_ERROR "[bench_regression] Benchmark binary exited ${_bench_rc}")
endif()

if(NOT EXISTS "${CURRENT_JSON}")
  message(FATAL_ERROR "[bench_regression] Benchmark did not produce output at ${CURRENT_JSON}")
endif()

# Run the comparison script.
message(STATUS "[bench_regression] Comparing against ${BASELINE_JSON} ...")
execute_process(
  COMMAND python3 "${TOOLS_DIR}/bench_compare.py" "${BASELINE_JSON}" "${CURRENT_JSON}"
  RESULT_VARIABLE _compare_rc
)

# bench_compare.py exits 0 (SOFT gate per [const §VIII.2]); propagate.
if(NOT _compare_rc EQUAL 0)
  message(WARNING "[bench_regression] bench_compare.py exited ${_compare_rc} (unexpected for SOFT gate)")
endif()
