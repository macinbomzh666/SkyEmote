#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Launch MSVC link tools through Wine without the FIFO redirection wrapper.

The msvc-wine wrapper can wait forever on its output FIFOs when Wine exits
before opening them. WINE_MSVC_RAW_STDOUT exposes diagnostics and exit codes
instead. This launcher affects link steps only: compiler wrappers still perform
their normal argument and include-output translation.

CommonLib's librarian invocation contains hundreds of object paths, exceeding
Windows' command-line limit even though the Linux host accepts it. For lib/lib.exe
we write those arguments to a temporary UTF-16 response file and pass just its
name. Windows quoting and Unix input-path translation are applied before writing.
The file is removed after the librarian exits. Other link commands pass through.

The toolchain routes manifest processing through msvc-mt.sh, which clears the
inherited raw-output flag: mt needs msvctricks' Windows exit-code translation.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    command = sys.argv[1:]
    environment = dict(os.environ, WINE_MSVC_RAW_STDOUT="1")
    if Path(command[0]).name.lower() not in {"lib", "lib.exe"}:
        return subprocess.call(command, env=environment)

    with tempfile.TemporaryDirectory(prefix=".msvc-link-", dir=Path.cwd()) as directory:
        response = Path(directory) / "objects.rsp"
        arguments = []
        for argument in command[1:]:
            # Response files bypass the wrapper's Unix-path translation.
            if argument.startswith("/") and Path(argument).parent != Path("/") and Path(argument).parent.exists():
                argument = "Z:" + argument
            arguments.append(subprocess.list2cmdline([argument]))
        response.write_text("\n".join(arguments), encoding="utf-16")
        return subprocess.call([command[0], "@" + str(response.relative_to(Path.cwd()))], env=environment)


if __name__ == "__main__":
    sys.exit(main())
