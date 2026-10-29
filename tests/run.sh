#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_build_dir=$(mktemp -d)
trap 'rm -rf "$test_build_dir"' EXIT
cxx=${CXX:-g++}
# LeakSanitizer cannot run under the cloud executor's ptrace. ASan and UBSan stay on.
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0"
"$cxx" -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    tests/test_engine_safety.cpp -o "$test_build_dir/safety"
"$test_build_dir/safety"
"$cxx" -std=c++11 -Itests/stubs -Wall -Wextra -Wno-unused-function \
    -Wno-unused-parameter -Wno-unused-variable -fsanitize=address,undefined -g \
    tests/test_engine_integration.cpp -o "$test_build_dir/integration"
"$test_build_dir/integration"
