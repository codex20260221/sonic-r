#!/usr/bin/env python3
"""Compile the real replay loader and run synthetic fixtures without SDL/assets."""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


REPO = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cl" if os.name == "nt" else "cc"))
    parser.add_argument("--source", type=Path, default=REPO / "source/sdl/src/save.c")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--case", choices=("all", "overflow", "truncated", "valid"), default="all")
    args = parser.parse_args()
    sources = [str(REPO / "tests/test_replay.c"), str(args.source.resolve())]
    extra_cflags = shlex.split(os.environ.get("CFLAGS", ""))
    extra_ldflags = shlex.split(os.environ.get("LDFLAGS", ""))

    with tempfile.TemporaryDirectory(prefix="sonicr-replay-") as temporary:
        build = Path(temporary)
        binary = build / ("test_replay.exe" if os.name == "nt" else "test_replay")
        if Path(args.cc).name.lower() in ("cl", "cl.exe"):
            # MSVC cannot parse the GNU attribute on the unused g_tileMap extern.
            # This test-only shim affects no replay data or Player layout.
            compat = build / "msvc-compat.h"
            compat.write_text("#define __attribute__(x)\n")
            command = [
                args.cc, "/nologo", "/std:c11", "/W3", "/WX", "/O1", "/Gy", "/Gw",
                "/DSONICR_SDL", "/D_CRT_SECURE_NO_WARNINGS",
                "/I" + str(REPO / "source/sdl/src"), "/FI" + str(compat),
                "/FI" + str(REPO / "tests/replay_test_io.h"),
                "/Fo" + build.as_posix() + "/", "/Fd" + str(build / "test.pdb"),
                "/Fe" + str(binary),
            ]
            if args.sanitize:
                command += ["/fsanitize=address", "/Zi"]
            command += extra_cflags + sources + ["/link", "/OPT:REF", "/INCREMENTAL:NO"] + extra_ldflags
        else:
            dead_strip = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
            command = [
                args.cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-result", "-ffunction-sections", "-fdata-sections", "-DSONICR_SDL",
                "-I" + str(REPO / "source/sdl/src"),
                "-include", str(REPO / "tests/replay_test_io.h"),
            ]
            if args.sanitize:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            command += extra_cflags + sources + [dead_strip] + extra_ldflags + ["-o", str(binary)]
        subprocess.run(command, cwd=build, check=True)
        subprocess.run([str(binary), args.case], cwd=build, check=True)


if __name__ == "__main__":
    main()
