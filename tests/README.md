# Replay loader regression checks

These checks compile the production `source/sdl/src/save.c` and use synthetic
replay files. No game assets, SDL libraries, window, audio device, network,
test framework, or WSL are required.

To additionally check privately supplied PC demos, pass their directory:

```powershell
python tests/run_replay.py --demo-dir C:\path\to\owned-game\BIN\DEMOS
python tests/run_replay.py --sanitize --demo-dir C:\path\to\owned-game\BIN\DEMOS
```

The 257 synthetic cases still run. Each external `.DEM` is opened read only
at its original path by the production loader. The harness independently
decodes its little-endian header and payload, checks all published fields,
track conversion, every sample and the untouched buffer tail, and prints the
15 header words and frame count. No external asset is copied into the repo.
An absent/empty demo directory is an error; omit the option for asset-free use.

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

## Other integration regressions

Run from the repository root in an MSYS2 UCRT64 shell (with native Python 3
on `PATH` for the Makefile check):

```sh
CC=gcc sh tests/run_settings.sh
CC=gcc sh tests/run_net_interp.sh
CC=gcc python tests/test_build_dependencies.py
```

These cover 168 settings cases, 5 interpolation tests, and development/release
incremental dependency scenarios. The settings harness supplies unused
`save.c` globals required by PE/COFF linkers. The dependency harness uses
relative Makefile variables to work with native Windows Python and MSYS Make.
See the [Windows integration record](../docs/windows-integration-2026-10-24.md)
for the fresh build and authentic demo results.

## Terrain loader

Run in UCRT64 with GCC, or a compatible POSIX compiler:

```sh
python tests/run_terrain.py --cc gcc
CFLAGS=-fanalyzer python tests/run_terrain.py --cc gcc
```

For Windows AddressSanitizer, use an x64 MSVC developer environment:

```powershell
python tests/run_terrain.py --cc cl --sanitize
python tests/run_terrain.py --cc cl --sanitize --terrain-dir "$env:SONICR_DATA_DIR/BIN/EXTRAS"
```

The optional directory supplies external `.TER` files read-only. No original
assets are required by the 2,500 synthetic cases. They cover the 80,493-byte
overflow, exact capacity, every shorter length of a 2,360-byte fixture, invalid
offsets/counts/alignment, face/wall/loop/grid/item references, fixed animation
item groups, invalid grid dimensions, parse-before-load, reloads and I/O failures.
Valid files retain their bytes and published pointers/scalars; invalid files
must take the failure-exit path before publishing invalid metadata. A rejected
load terminates the game; the checks do not imply rollback of partially read
buffer contents.

GCC and MSVC compile the complete production `track_load.c`. Unrelated
renderer/audio entry points have aborting stubs for PE/COFF linking. MSVC uses
a test-only shim for GNU attributes; production declarations are unchanged.
The harness forwards real stdio calls, injects deterministic I/O errors and
intercepts failure exits. Fixtures and binaries use temporary directories.
`--source` selects an exported production baseline; `--case overflow --sanitize`
reproduces the original write with MSVC ASan, while `--case valid` checks accepted
files independently of the new validation. See the
[terrain audit record](../docs/terrain-loading-2026-10-25.md).

## Network delta codec

```powershell
python tests/run_net_delta.py
python tests/run_net_delta.py --sanitize
```

Or use `--cc gcc` in UCRT64. The 140 cases cover all truncation points in
representative keyframes, multi-field relative deltas and overflow-submask
deltas; unchanged destination/history on failure; retransmission; missing
history; signed values; staged publication; and literal valid wire fixtures.
`--source path/to/net_delta.c --case atomic` can reproduce the pre-fix history
corruption using an exported baseline. `--case valid` checks four golden
wire/missing-history cases without depending on the new staging API.

## Network packet receive

Run with GCC in UCRT64, or a compatible POSIX compiler on other platforms:

```sh
python tests/run_net_receive.py --cc gcc
CFLAGS=-fanalyzer python tests/run_net_receive.py --cc gcc
```

The 615 cases include every shorter byte length of representative four-player
keyframe/delta snapshots and client-input packets, complete retransmission
after failure, malformed count/slot/framing data, local-slot validation,
missing histories, duplicates/reordering, 16-bit sequence wrap, all supported
snapshot counts, version-0 markers, input-only packets, lobby frame handling,
and finish order in arbitrary wire order.

The harness includes the complete production `network.c` and `net_interp.c`
to inspect private state, links the real codec and game global definitions,
and calls `ApplyNetworkPlayerState()`. Only the transport, clock, logging and
unrelated platform calls are stubbed. Unexpected sends/platform work fail;
no real sockets, matchmaking, receive thread, window or assets are involved.
Rejected packets must leave all players, receive histories, sequence numbers,
inputs/receive flags, peer versions, timestamps, session frames, interpolation
history and finish bookkeeping unchanged.

`--source` and `--delta-source` select exported production sources. The cases
`snapshot`, `snapshot-late`, `input` and `decoder` isolate the original defects.
`--sanitize` enables GCC/Clang ASan/UBSan where supported; UCRT64 here lacks
`libasan`, so packet sanitizer coverage is not claimed. MSVC ASan covers the
codec separately. These tests do not test real transport or concurrency.
See [network receive verification](../docs/network-receive-2026-10-24.md).

## Loss, duplication and reordering

```sh
python tests/run_net_loss.py --cc gcc
CFLAGS=-fanalyzer python tests/run_net_loss.py --cc gcc
python tests/run_net_loss.py --cc gcc --case profile
```

The isolated delivery harness reuses the production packet receiver and actual
snapshot sender, the production input codec with its existing envelope, and the
prior transaction fixture. It opens no sockets. The 96 scenarios / 8,822
deliveries cover all 31 loss phases, isolated/burst loss, lost initial/periodic
and consecutive keyframes, duplicates, reordering with/without retransmission,
stale and malformed packets, wraparound, independently sequenced clients,
input-only packets including a full-wrap baseline alias, and partial keyframe
recovery. Accepted state is compared
with an uninterrupted authoritative decode; rejected state is compared byte
for byte with its pre-delivery state.

To reproduce the failure without changing branches:

```powershell
python -c 'from pathlib import Path; import subprocess; p=Path("source/sdl/build/loss-evidence/baseline-network.c"); p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(subprocess.check_output(["git","show","5193ef1:source/sdl/src/net/network.c"]))'
```

```sh
python tests/run_net_loss.py --cc gcc --source source/sdl/build/loss-evidence/baseline-network.c --case profile
```

The baseline profile deliberately exits nonzero after reporting corruption.
Baseline and repaired profile sender fingerprints must match. See the
[recovery record](../docs/network-loss-2026-10-24.md) for exact measurements,
conditional recovery bounds and compatibility limitations.

## Two Windows game instances

After building the development executable, from PowerShell 7:

```powershell
./tests/windows_lan.ps1
./tests/windows_lan.ps1 -Status
./tests/windows_lan.ps1 -Stop
```

This copies private data into separate ignored host/client directories, copies
the same executable under distinct names, and launches both with `--lan`.
The client also receives `--host 127.0.0.1`. No `ONLINE.DAT` is copied and the
original data is not used as a writable game directory. `-PrepareOnly` prepares
without launching. The manifest records exact executable paths, SHA-256,
arguments, PIDs and start times; status/stop verify process identity before use.
It reuses existing prepared copies and never deletes them. Use a fresh
`-SessionDirectory` under ignored `source/sdl/build` to recopy changed assets.

Menus still require manual interaction. See the
[Windows LAN workflow and observed limitations](../docs/windows-lan-2026-10-24.md).
