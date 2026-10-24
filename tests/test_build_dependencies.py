#!/usr/bin/env python3
"""Exercise the SDL Makefile's incremental builds without SDL or game assets."""

import os
from pathlib import Path
import subprocess
import tempfile
import time


REPO = Path(__file__).resolve().parents[1]
MAKEFILE = REPO / "source" / "sdl" / "Makefile"


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def exercise(root, release):
    source = root / "src"
    (source / "nested").mkdir(parents=True)
    (source / "main.c").write_text("int value(void);\nint main(void) { return value(); }\n")
    header = source / "nested" / "value.h"
    header.write_text("#define VALUE 3\n")
    implementation = source / "nested" / "value.c"
    implementation.write_text('#include "value.h"\nint value(void) { return VALUE; }\n')

    build = root / ("build-release" if release else "build")
    binary_name = "sonicr.exe" if os.name == "nt" else "sonicr"
    target = build / binary_name
    command = [
        os.environ.get("MAKE", "make"), "--no-print-directory", "-f", MAKEFILE.as_posix(),
        # Relative forward-slash paths also work with MSYS Make + native Python.
        "SRCDIR=src", "BUILDDIR=build", "RELEASE_BUILDDIR=build-release",
        "TARGET=build/" + binary_name,
        "RELEASE_TARGET=build-release/" + binary_name,
        "CC=" + os.environ.get("CC", "cc"),
        "CFLAGS=-std=c11 -Wall -Wextra -Werror",
        "RELEASE_CFLAGS=-std=c11 -Wall -Wextra -Werror",
        "LDFLAGS=", "RELEASE_LDFLAGS=", "MINIUPNPC_PC=no",
    ]
    # No explicit development goal: included .d files must not replace `all`.
    if release:
        command.append("release")

    def make():
        result = subprocess.run(command, cwd=root, text=True, capture_output=True)
        check(result.returncode == 0, result.stdout + result.stderr)

    def run():
        return subprocess.run([str(target)], cwd=root, check=False).returncode

    make()
    check(run() == 3, "initial fixture did not build correctly")
    unrelated = build / "main.o"
    unrelated_mtime = unrelated.stat().st_mtime_ns
    target_mtime = target.stat().st_mtime_ns
    make()
    check(target.stat().st_mtime_ns == target_mtime, "no-op build relinked the executable")

    # Also works on filesystems with one-second timestamp resolution.
    time.sleep(1.1)
    header.write_text("#define VALUE 7\n")
    make()
    check(run() == 7, "header edit left a stale object/executable")
    check(unrelated.stat().st_mtime_ns == unrelated_mtime,
          "header edit rebuilt an unrelated translation unit")

    # A stale .d prerequisite must not prevent removing a now-unused header.
    time.sleep(1.1)
    implementation.write_text("int value(void) { return 9; }\n")
    header.unlink()
    make()
    check(run() == 9, "removing an unused header broke the incremental build")
    print(f"{'release' if release else 'development'} dependencies: PASS")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="sonicr-build-test-") as temporary:
        for release in (False, True):
            exercise(Path(temporary) / ("release" if release else "development"), release)
