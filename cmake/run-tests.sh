#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

# Stage the matching CRT beside the test executable for app-local DLL lookup.
source "$(dirname "$(command -v cl)")/msvcenv.sh"
for library in msvcp140 vcruntime140 vcruntime140_1; do
    cp "$BINDIR/$library.dll" "$(dirname "$1")/"
done
export WINEDLLOVERRIDES="msvcp140=n;${WINEDLLOVERRIDES:-}"
export WINEDEBUG="${WINEDEBUG:--all},err+module"
exec wine "$@"
