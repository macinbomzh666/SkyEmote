#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

: "${WINEPREFIX:?Set WINEPREFIX to your MSVC Wine prefix}"
for script in tools/check.sh cmake/run-tests.sh cmake/msvc-mt.sh cmake/dependencies/directxtk-shaders.sh; do
    bash -n "$script"
done
python3 -m py_compile cmake/msvc-link.py tools/verify_package.py
python3 -m json.tool CMakePresets.json > /dev/null

cmake --preset dependencies
cmake --build --preset dependencies
cmake --preset checks
cmake --build --preset checks
ctest --preset checks
cmake --build --preset linux
python3 tools/verify_package.py build/linux/SkyEmote.zip
