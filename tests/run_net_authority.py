#!/usr/bin/env python3
"""Test packet authority with the production receiver and loopback-only UDP."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[1]


def main(harness="authority"):
    description = __doc__ if harness == "authority" else "Exercise real lobby exits and transport recreation without game assets."
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc" if os.name == "nt" else "cc"))
    parser.add_argument("--source", type=Path, default=REPO / "source/sdl/src/net/network.c")
    parser.add_argument("--transport-source", type=Path, default=REPO / "source/sdl/src/net/net_transport.c")
    if harness == "lifecycle":
        parser.add_argument("--lobby-source", type=Path, default=REPO / "source/sdl/src/screen_misc.c")
        parser.add_argument("--main-source", type=Path, default=REPO / "source/sdl/src/main.c")
    parser.add_argument("--case", default="all",
                        choices=(("all", "timeout", "escape", "handoff", "reset", "recreate", "queue", "resources", "departure", "liveness", "rejoin", "loading") if harness == "lifecycle" else
                                 ("all", "valid", "input", "snapshot", "control", "fields", "malformed",
                                  "stale", "loopback", "crash", "barrier", "wait")))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--ucrt64-dir", type=Path,
                        help="existing UCRT64 prefix for MSVC's pthread headers/import library; defaults to gcc on PATH")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix=f"sonicr-{harness}-") as temporary:
        build = Path(temporary)
        binary = build / (harness + (".exe" if os.name == "nt" else ""))
        msvc = Path(args.cc).name.lower() in ("cl", "cl.exe")
        definitions = ["SONICR_SDL",
                       'SONICR_TEST_NETWORK_SOURCE="' + args.source.resolve().as_posix() + '"',
                       'SONICR_TEST_TRANSPORT_SOURCE="' + args.transport_source.resolve().as_posix() + '"']
        if harness == "lifecycle":
            definitions += ['SONICR_TEST_LOBBY_SOURCE="' + args.lobby_source.resolve().as_posix() + '"',
                            'SONICR_TEST_MAIN_SOURCE="' + args.main_source.resolve().as_posix() + '"']
        if ("int net_recv_packet(" in args.transport_source.read_text(encoding="utf-8") and
                "pkt->generation" in args.source.read_text(encoding="utf-8")):
            definitions += ["SONICR_TEST_GENERATIONS"]
        if "int NetLobbyHostLost(void)" in args.source.read_text(encoding="utf-8"):
            definitions += ["SONICR_TEST_LOBBY_LIVENESS"]
        if msvc:
            prefix = args.ucrt64_dir
            if prefix is None:
                gcc = shutil.which("gcc")
                if gcc is None:
                    parser.error("MSVC requires --ucrt64-dir or an installed UCRT64 gcc on PATH")
                prefix = Path(gcc).resolve().parents[1]
            for name in ("pthread.h", "pthread_compat.h", "pthread_time.h", "pthread_unistd.h", "sched.h"):
                shutil.copyfile(prefix / "include" / name, build / name)
            compat = build / "msvc-compat.h"
            compat.write_text('#define __attribute__(x)\n'
                              '#ifndef WIN32_LEAN_AND_MEAN\n#define WIN32_LEAN_AND_MEAN\n#endif\n'
                              '#include <winsock2.h>\n#include <stdint.h>\n'
                              'typedef intptr_t ssize_t;\ntypedef int socklen_t;\n', encoding="utf-8")
            (build / "unistd.h").write_text('#pragma once\n#include <windows.h>\n'
                                           'static int usleep(unsigned usec) { Sleep((usec + 999) / 1000); return 0; }\n',
                                           encoding="utf-8")
            if harness == "lifecycle":
                with (build / "unistd.h").open("a", encoding="utf-8") as header:
                    header.write('#include <direct.h>\n#define chdir _chdir\n')
                (build / "getopt.h").write_text('struct option { const char *name; int has_arg; int *flag; int val; };\n'
                                              'enum { no_argument, required_argument, optional_argument };\n'
                                              'extern char *optarg; extern int optind;\n'
                                              'int getopt_long(int, char *const *, const char *, const struct option *, int *);\n',
                                              encoding="utf-8")
            definitions += ["_CRT_SECURE_NO_WARNINGS", "_WINSOCK_DEPRECATED_NO_WARNINGS", "WINPTHREADS_USE_DLLIMPORT"]
            command = [args.cc, "/nologo", "/std:c11", "/W3", "/WX", "/O1", "/Gy", "/Gw",
                       "/I" + str(build), "/I" + str(REPO / "source/sdl/src"),
                       "/I" + str(REPO / "source/sdl/src/net"), "/FI" + str(compat),
                       "/Fo" + build.as_posix() + "/", "/Fd" + str(build / "test.pdb"),
                       "/Fe" + str(binary)]
            command += ["/D" + value for value in definitions]
            if args.sanitize:
                command += ["/fsanitize=address", "/Zi"]
        else:
            command = [args.cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-result", "-Wno-strict-aliasing", "-D_GNU_SOURCE",
                       "-ffunction-sections", "-fdata-sections", "-pthread",
                       "-I" + str(REPO / "source/sdl/src"), "-I" + str(REPO / "source/sdl/src/net")]
            command += ["-D" + value for value in definitions]
            if args.sanitize:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        command += shlex.split(os.environ.get("CFLAGS", ""))
        command += [str(REPO / f"tests/test_net_{harness}.c"),
                    str(REPO / "source/sdl/src/net/net_delta.c"),
                    str(REPO / "source/sdl/src/globals.c"),
                    str(REPO / "source/sdl/src/globals_extra.c")]
        if harness == "lifecycle":
            command += [str(REPO / "tests/net_lifecycle_lobby.c"), str(REPO / "tests/net_lifecycle_main.c")]
            if not msvc:
                # Same suppression as the desktop build for unrelated credits
                # rendering in the complete screen_misc translation unit.
                command += ["-Wno-maybe-uninitialized"]
            if os.name == "nt":
                command += [str(REPO / "tests/net_lifecycle_unused.c")]
        if msvc:
            command += ["/link", "/OPT:REF", "/INCREMENTAL:NO", "ws2_32.lib", "iphlpapi.lib",
                        str(prefix / "lib/libwinpthread.dll.a")]
            if harness == "lifecycle":
                command += ["winmm.lib"]
        else:
            command += ["-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections", "-o", str(binary)]
        if os.name == "nt" and not msvc:
            command += ["-lws2_32", "-liphlpapi"]
            if harness == "lifecycle":
                command += ["-lwinmm"]
        command += shlex.split(os.environ.get("LDFLAGS", ""))
        subprocess.run(command, cwd=build, check=True)
        environment = os.environ.copy()
        if msvc:
            environment["PATH"] = str(prefix / "bin") + os.pathsep + environment.get("PATH", "")
        timeout = 45 if harness == "lifecycle" else 20
        try:
            result = subprocess.run([str(binary), args.case], cwd=build, text=True, capture_output=True,
                                    encoding="utf-8", errors="replace", timeout=timeout, env=environment)
        except subprocess.TimeoutExpired:
            raise SystemExit(f"network {harness}: {args.case} timed out ({timeout} seconds)")
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr[-16000:], file=sys.stderr, end="")
            raise SystemExit(1)


if __name__ == "__main__":
    main()
