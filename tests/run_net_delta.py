#!/usr/bin/env python3
"""Compile the production delta codec and deterministic, asset-free regressions."""
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
    parser.add_argument("--source", type=Path, default=REPO / "source/sdl/src/net/net_delta.c")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--case", choices=("all", "atomic", "valid"), default="all")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="sonicr-delta-") as temporary:
        build = Path(temporary)
        binary = build / ("test_net_delta.exe" if os.name == "nt" else "test_net_delta")
        sources = [str(REPO / "tests/test_net_delta.c"), str(args.source.resolve())]
        if Path(args.cc).name.lower() in ("cl", "cl.exe"):
            command = [args.cc, "/nologo", "/std:c11", "/W3", "/WX", "/O1",
                       "/I" + str(REPO / "source/sdl/src"),
                       "/I" + str(REPO / "source/sdl/src/net"),
                       "/Fo" + build.as_posix() + "/", "/Fd" + str(build / "test.pdb"),
                       "/Fe" + str(binary)]
            if args.sanitize:
                command += ["/fsanitize=address", "/Zi"]
            command += shlex.split(os.environ.get("CFLAGS", "")) + sources
            command += ["/link", "/INCREMENTAL:NO"]
        else:
            command = [args.cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                       "-I" + str(REPO / "source/sdl/src"),
                       "-I" + str(REPO / "source/sdl/src/net")]
            if args.sanitize:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            command += shlex.split(os.environ.get("CFLAGS", "")) + sources + ["-o", str(binary)]
        command += shlex.split(os.environ.get("LDFLAGS", ""))
        subprocess.run(command, cwd=build, check=True)
        subprocess.run([str(binary), args.case], cwd=build, check=True)


if __name__ == "__main__":
    main()
