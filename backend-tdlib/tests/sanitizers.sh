#!/usr/bin/env bash
set -euo pipefail
build=${1:?build directory required}
shift
cmake_command=("$@")
if test "${#cmake_command[@]}" -eq 0; then cmake_command=(cmake); fi
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# Reuse third-party objects; sanitizer flags apply to all TeleBezel and GoogleTest targets.
for sanitizer in address thread leak; do
  echo "Running ${sanitizer} sanitizer"
  "${cmake_command[@]}" -S "$root" -B "$build" \
    -DTELEBEZEL_BUILD_TESTS=ON -DTELEBEZEL_SANITIZER="$sanitizer"
  "${cmake_command[@]}" --build "$build" --target telebezel-tdlib-tests telebezel-tdlib-runtime-tests telebezel-tdlib-failure-tests --parallel 2
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    CTEST_OUTPUT_ON_FAILURE=1 TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1 LSAN_OPTIONS=exitcode=23 \
    "${cmake_command[@]}" --build "$build" --target test
done
