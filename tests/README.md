# Replay loader regression checks

These checks compile the production `source/sdl/src/save.c` and use synthetic
replay files. No game assets, SDL libraries, window, audio device, network,
test framework, or WSL are required.

On native Windows, use an x64 Native Tools prompt for Visual Studio 2022, or
PowerShell with the MSVC compiler environment loaded:

```powershell
python tests/run_replay.py
python tests/run_replay.py --sanitize
```

Python 3 and a C11 compiler are required. The default compiler is `cl` on
Windows and `cc` elsewhere. With GCC/Clang, including MSYS2/MinGW:

```sh
python3 tests/run_replay.py --cc gcc
python3 tests/run_replay.py --cc gcc --sanitize
```

`--sanitize` selects MSVC AddressSanitizer or GCC/Clang AddressSanitizer and
UBSan. `CC` (one executable), `CFLAGS`, and `LDFLAGS` are also accepted. Outputs
and fixture files live in a temporary directory that is removed after the run.

The 257 cases cover:

- The original 32,769-frame overflow and aggregate overflow with 2–4 viewports.
- Every shorter byte length of representative 1–4-viewport files, including
  all 30 header bytes, plus truncation at the maximum buffer capacity.
- Missing files, invalid counts, unsupported metadata, and invalid active
  character IDs, with complete game/replay state preservation on failure.
- Exact capacity boundaries, zero-frame recordings, all five track IDs and
  their existing 3/4 conversion, supported modes, player counts and characters,
  signed inactive character fields, trailing bytes, and unchanged buffer tails.
- Failed reload after success, allocation failure, short header/payload reads,
  stream errors, close errors, and release of staging memory and file handles.

The loader is compiled as a separate translation unit. The force-included
`replay_test_io.h` forwards its C-library calls and allows deterministic error
injection; it does not replace the parser. The MSVC runner also supplies a
temporary compatibility macro for the GNU alignment attribute on the unused
`g_tileMap` extern. No production layout is changed for the tests.

To reproduce against unmodified main without checking out another branch,
export its source as bytes (PowerShell-compatible):

```powershell
python -c "from pathlib import Path; import subprocess; p=Path('source/sdl/build/replay-baseline.c'); p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(subprocess.check_output(['git', 'show', '3da98f2:source/sdl/src/save.c']))"
python tests/run_replay.py --source source/sdl/build/replay-baseline.c --case valid
python tests/run_replay.py --source source/sdl/build/replay-baseline.c --case truncated
python tests/run_replay.py --source source/sdl/build/replay-baseline.c --case overflow --sanitize
```

The 34 valid cases pass on both versions. Baseline `truncated` fails on a
complete header followed by five of six declared payload bytes, without
depending on uninitialized header data. Baseline `overflow` produces an ASan
global-buffer-overflow; these two commands are expected to exit nonzero.

Windows x64 MSVC 19.44.35228 and Python 3.12.10 were exercised. GCC/Clang/macOS
runner paths were reviewed but not executed in this environment. The
`SONICR_FLOAT32` check covers shared type declarations, not SH4 execution.
These subsystem results do not establish original-game equivalence or full
Windows, macOS, Linux, or Dreamcast build/runtime correctness. See the
[verification record](../docs/replay-loader-review-2026-10-24.md).
