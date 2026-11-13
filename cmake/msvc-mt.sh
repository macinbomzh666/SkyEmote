#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

# msvctricks maps mt's Windows "manifest updated" status for CMake on Linux.
unset WINE_MSVC_RAW_STDOUT
exec mt "$@"
