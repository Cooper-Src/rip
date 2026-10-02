#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-linux"

cd "${ROOT}"

echo "========================================"
echo "RIP - Linux Build"
echo "========================================"
echo

cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --parallel

echo
echo "========================================"
echo "Build successful"
echo "========================================"
echo
echo "CLI:"
echo "  ${BUILD_DIR}/rip"
