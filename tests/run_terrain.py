#!/usr/bin/env python3
"""Exercise the production terrain loader with synthetic and optional private files."""
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
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc" if os.name == "nt" else "cc"))
    parser.add_argument("--source", type=Path, default=REPO / "source/sdl/src/track_load.c")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--case", choices=("all", "valid", "overflow", "truncated", "header", "references", "io"),
                        default="all")
    parser.add_argument("--terrain-dir", type=Path, help="also check external .TER files read-only")
    args = parser.parse_args()
    terrains = []
    if args.terrain_dir is not None:
        if not args.terrain_dir.is_dir():
            parser.error("--terrain-dir must be an existing directory")
        terrains = sorted(p.resolve() for p in args.terrain_dir.iterdir()
                          if p.is_file() and p.suffix.lower() == ".ter")
        if not terrains:
            parser.error("--terrain-dir contains no .TER files")
    msvc = Path(args.cc).name.lower() in ("cl", "cl.exe")
    with tempfile.TemporaryDirectory(prefix="sonicr-terrain-") as temporary:
        build = Path(temporary)
        binary = build / ("test_terrain.exe" if os.name == "nt" else "test_terrain")
        source = args.source.resolve()
        if msvc:
            compat = build / "msvc-compat.h"
            compat.write_text("#define __attribute__(x)\n")
            command = [args.cc, "/nologo", "/std:c11", "/W3", "/WX", "/O1", "/Gy", "/Gw",
                       "/DSONICR_SDL", "/D_CRT_SECURE_NO_WARNINGS",
                       "/I" + str(REPO / "source/sdl/src"), "/FI" + str(compat),
                       "/FI" + str(REPO / "tests/terrain_test_io.h"),
                       "/Fo" + build.as_posix() + "/", "/Fd" + str(build / "test.pdb"),
                       "/Fe" + str(binary)]
            if args.sanitize:
                command += ["/fsanitize=address", "/Zi"]
        else:
            command = [args.cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-result", "-ffunction-sections", "-fdata-sections", "-DSONICR_SDL",
                       "-I" + str(REPO / "source/sdl/src"),
                       "-include", str(REPO / "tests/terrain_test_io.h")]
            if args.sanitize:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        command += shlex.split(os.environ.get("CFLAGS", ""))
        command += [str(REPO / "tests/test_terrain.c"), str(source)]
        if msvc:
            command += ["/link", "/OPT:REF", "/INCREMENTAL:NO"]
        else:
            command += ["-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
                        "-o", str(binary)]
        command += shlex.split(os.environ.get("LDFLAGS", ""))
        subprocess.run(command, cwd=build, check=True)
        runs = [[str(binary), args.case]]
        if terrains:
            runs.append([str(binary), "external", *map(str, terrains)])
        for run in runs:
            result = subprocess.run(run, cwd=build, text=True, capture_output=True)
            print(result.stdout, end="")
            if result.returncode:
                print(result.stderr[-16000:], file=sys.stderr, end="")
                raise SystemExit(1)


if __name__ == "__main__":
    main()
