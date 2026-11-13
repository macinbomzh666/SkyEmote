#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

# DirectXTK's batch file needs FXC on Wine's PATH and a Windows output path.
source "$(dirname "$(command -v cl)")/msvcenv.sh"
# FXC needs the SDK's compiler DLL. Wine's implementation rejects these shaders.
export WINEDLLOVERRIDES="d3dcompiler_47=n;${WINEDLLOVERRIDES:-}"
export CompileShadersOutput="$(winepath -w "${CompileShadersOutput//\"/}")"
exec wine cmd.exe /d /c CompileShaders.cmd "$@"
