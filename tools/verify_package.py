#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify the local mod archive against its runtime inputs and built DLL."""
import argparse
from pathlib import Path
import posixpath
import re
import struct
import subprocess
import sys
import zipfile
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
DLL = "SKSE/Plugins/SkyEmote.dll"
NOTICE = "licenses/SkyEmote/THIRD_PARTY_NOTICES.md"


def verify_notice_links(package):
    """Resolve the notice's relative links inside the installable archive."""
    names = set(package.namelist())
    text = package.read(NOTICE).decode("utf-8")
    for target in re.findall(r"\[[^\]]+\]\(([^)]+)\)", text):
        link = urlsplit(target)
        if link.scheme or link.netloc or not link.path:
            continue
        destination = posixpath.normpath(posixpath.join(posixpath.dirname(NOTICE), unquote(link.path)))
        if destination not in names:
            raise ValueError(f"Broken packaged notice link: {target} (expected {destination})")


def verify(archive):
    expected = {DLL: (archive.parent / "SkyEmote.dll").read_bytes()}
    for path in (ROOT / "assets").rglob("*"):
        if path.is_file():
            expected[path.relative_to(ROOT / "assets").as_posix()] = path.read_bytes()
    for path in [ROOT / "LICENSE", ROOT / "THIRD_PARTY_NOTICES.md"]:
        expected["licenses/SkyEmote/" + path.name] = path.read_bytes()
    for path in (ROOT / "licenses").iterdir():
        if path.is_file():
            expected["licenses/SkyEmote/licenses/" + path.name] = path.read_bytes()

    with zipfile.ZipFile(archive) as package:
        members = [entry for entry in package.infolist() if not entry.is_dir()]
        names = [entry.filename for entry in members]
        if len(names) != len(set(names)) or set(names) != expected.keys():
            raise ValueError("Archive has duplicate, missing or unexpected runtime files")
        directories = {parent.as_posix() + "/" for name in expected
                       for parent in Path(name).parents if parent != Path(".")}
        if any(entry.is_dir() and entry.filename not in directories for entry in package.infolist()):
            raise ValueError("Archive has an unexpected directory")
        for entry in members:
            content = expected[entry.filename]
            if entry.file_size != len(content) or package.read(entry) != content:
                raise ValueError("Archive content differs from build input: " + entry.filename)
        if package.testzip() is not None:
            raise ValueError("Archive CRC verification failed")
        verify_notice_links(package)

    raw = expected[DLL]
    if raw[:2] != b"MZ":
        raise ValueError("Plugin is not a Windows executable")
    offset = struct.unpack_from("<I", raw, 0x3C)[0]
    if (raw[offset:offset + 4] != b"PE\0\0" or
            struct.unpack_from("<H", raw, offset + 4)[0] != 0x8664 or
            struct.unpack_from("<H", raw, offset + 24)[0] != 0x20B or
            not struct.unpack_from("<H", raw, offset + 22)[0] & 0x2000):
        raise ValueError("Plugin must be a Windows x64 DLL")
    output = subprocess.check_output(["objdump", "-p", str(archive.parent / "SkyEmote.dll")], text=True)
    for export in ("SKSEPlugin_Load", "SKSEPlugin_Version", "SKSEPlugin_Query"):
        if not re.search(r"^\s*\[\s*\d+\].*\s" + export + r"\s*$", output, re.MULTILINE):
            raise ValueError("Plugin export missing: " + export)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, nargs="?", default=ROOT / "build/linux/SkyEmote.zip")
    args = parser.parse_args()
    try:
        verify(args.archive.resolve())
    except (OSError, ValueError, struct.error, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        print(f"Package verification failed: {error}", file=sys.stderr)
        return 1
    print(f"Verified {args.archive}: runtime files, licenses, CRC and Windows x64 SKSE exports")
    return 0


if __name__ == "__main__":
    sys.exit(main())
