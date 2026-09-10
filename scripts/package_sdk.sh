#!/usr/bin/env bash
# 内部打包：Release 构建客户版（不要加 TIRAY_SDK_INTERNAL_BUILD）。
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/cmake-build-release"
OUT_DIR="${ROOT_DIR}/dist"
VERSION="${1:-0.1.0}"
PACKAGE_NAME="tiray-sdk-${VERSION}-$(uname -m)"
STAGE_DIR="${OUT_DIR}/${PACKAGE_NAME}"

rm -rf "${STAGE_DIR}"
mkdir -p "${OUT_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DTIRAY_SDK_BUILD_EXAMPLES=ON
cmake --build "${BUILD_DIR}" -j"$(nproc)"
(cd "${BUILD_DIR}" && ctest --output-on-failure)
cmake --install "${BUILD_DIR}" --prefix "${STAGE_DIR}"
tar -C "${OUT_DIR}" -czf "${OUT_DIR}/${PACKAGE_NAME}.tar.gz" "${PACKAGE_NAME}"
echo "${OUT_DIR}/${PACKAGE_NAME}.tar.gz"
