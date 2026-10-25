#!/usr/bin/env python3
"""Exercise production network dispatch with in-memory transport and a fixed clock."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[1]


def main(harness="receive"):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc" if os.name == "nt" else "cc"))
    parser.add_argument("--source", type=Path, default=REPO / "source/sdl/src/net/network.c")
    parser.add_argument("--delta-source", type=Path, default=REPO / "source/sdl/src/net/net_delta.c")
    choices = (("all", "profile", "snapshot", "input", "edges", "input-wrap") if harness == "loss" else
               ("all", "snapshot", "input", "valid", "snapshot-late", "decoder"))
    parser.add_argument("--case", choices=choices, default="all")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="sonicr-receive-") as temporary:
        build = Path(temporary)
        binary = build / ("test_net_receive.exe" if os.name == "nt" else "test_net_receive")
        dead_strip = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        command = [args.cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-result", "-Wno-strict-aliasing", "-DSONICR_SDL", "-D_GNU_SOURCE",
                   "-ffunction-sections", "-fdata-sections", "-pthread",
                   "-I" + str(REPO / "source/sdl/src"),
                   "-I" + str(REPO / "source/sdl/src/net"),
                   '-DSONICR_TEST_NETWORK_SOURCE="' + args.source.resolve().as_posix() + '"']
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        command += shlex.split(os.environ.get("CFLAGS", ""))
        test_source = "test_net_loss.c" if harness == "loss" else "test_net_receive.c"
        command += [str(REPO / "tests" / test_source), str(args.delta_source.resolve()),
                    str(REPO / "source/sdl/src/globals.c"),
                    str(REPO / "source/sdl/src/globals_extra.c"), dead_strip]
        command += shlex.split(os.environ.get("LDFLAGS", "")) + ["-o", str(binary)]
        subprocess.run(command, cwd=build, check=True)
        subprocess.run([str(binary), args.case], cwd=build, check=True)


if __name__ == "__main__":
    main()
