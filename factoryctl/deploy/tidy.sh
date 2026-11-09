#!/usr/bin/env bash
# clang-tidy over the server's sources and tests (.clang-tidy; server/tests/.clang-tidy for the
# tests). Every finding is an error: the check is clean or it fails. Run it before a commit.
#   deploy/tidy.sh [regex]     regex: only the files it matches (default: all of ours)
set -euo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)
build=$here/build/linux/linux_clang_debug
# The compile database and the generated headers (the embedded schema) come with a build.
(cd "$here" && cmake --build --preset linux_clang_debug >/dev/null)
run-clang-tidy -p "$build" -j "$(nproc)" -quiet "${1:-factoryctl/server/(src|tests)/}"
