# Windows integration baseline — 2026-10-24

Work stays on `codex/windows-playable-20261024` in the independent fork.
Initial HEAD was `c13a323892376e183f06c14466c4c16637233517`, with a clean
working tree. `origin` points to `codex20260221/sonic-r`; `upstream` points to
`jnmartin84/sonic-r`. No remote writes or hosted CI were used.

The integrated contributions are `b86bbd5` (bounded replay loader), `9f4200d`
(interpolation reset/slot ownership), `1d41273` (truncated settings), and
`c13a323` (desktop build/header dependencies). Comparing their production
files with HEAD found only the intentional settings change after the replay
commit; interpolation production files are identical to their contribution.
This baseline extension changes test harnesses and documentation only.

## Native build and checks

Windows 11, MSYS2 UCRT64 GCC 16.2.0, GNU Make 4.4.1, native Python 3.12.10,
and Visual Studio 2022 Build Tools were used. In UCRT64, add native Python to
PATH if it is not inherited:

```sh
# Ensure Python 3 is available on PATH; do not commit machine-specific setup.
python tests/run_replay.py --cc gcc --demo-dir /path/to/owned-game/BIN/DEMOS
CC=gcc sh tests/run_settings.sh
CC=gcc sh tests/run_net_interp.sh
CC=gcc python tests/test_build_dependencies.py
cd source/sdl
mkdir -p build
make -j8 CC=gcc BUILDDIR=build/integration-baseline TARGET=build/integration-baseline/sonicr.exe
```

The new build directory had no prior objects. The complete development build
passed with `-Werror` from the initial HEAD. Executable:
`source/sdl/build/integration-baseline/sonicr.exe`
(10,017,758 bytes), SHA-256
`7198417dac07593625107d2f86dc78b5e3d4f308092b5e559303e036a6fe717c`.
The ignored build log is `source/sdl/build/integration-baseline-build.log`.

A bounded offline launch with `<private-data-directory>` stayed running
for 20 seconds, created a window, and logged both logo screens followed by
`TitleScreen`. Closing that process's main window exited cleanly with code 0.
Logs are in ignored `source/sdl/build/integration-baseline-smoke.*.log`.
This is startup evidence, not visual inspection or a completed gameplay test.

| Check | Result |
| --- | --- |
| Replay, unmodified HEAD, GCC and MSVC | 257 synthetic cases pass on each |
| Replay, unmodified HEAD, MSVC ASan | 257 synthetic cases pass |
| Extended replay, GCC and MSVC ASan | 257 synthetic + 5 authentic cases pass on each |
| Settings, UCRT64 | 168 cases pass after adding unused link globals to harness |
| Interpolation, UCRT64 | 5 tests pass before and after harness changes |
| Make dependencies, UCRT64 | Development and release scenarios pass after fixing harness paths |

The existing settings harness initially failed to link on PE/COFF because
discarded functions still left `.refptr` references. The dependency harness
initially passed native backslash paths to Make's shell commands. Supplying
unused data symbols and using relative forward-slash paths fixes these
harness portability defects without changing production behavior.

## Authentic PC replay results

Files remain outside Git in `<private-demo-directory>`. Header words
are: viewports, players, humans, disk track, race type, submode, difficulty,
weather, time of day, five character IDs, frames per viewport. The harness
compares the production loader's state to independently decoded bytes, including
all payload samples, unrelated player fields and the remaining ghost buffer.

| File | Bytes | Viewports / players / humans | Disk → runtime track | Race / submode / difficulty | Weather / time | Characters | Frames per viewport / samples |
| --- | ---: | --- | --- | --- | --- | --- | ---: |
| CITY.DEM | 3,834 | 1 / 5 / 1 | 2 → 2 | 0 / 0 / 1 | 0 / 2 | 2,0,1,3,4 | 1,902 / 1,902 |
| EMERALD2.DEM | 10,450 | 1 / 5 / 1 | 5 → 5 | 0 / 0 / 1 | 0 / 3 | 8,9,7,5,6 | 5,210 / 5,210 |
| FACTORY.DEM | 5,674 | 2 / 2 / 2 | 3 → 4 | 1 / 0 / 1 | 0 / 3 | 0,1,2,3,4 | 1,411 / 2,822 |
| ISLAND.DEM | 2,174 | 1 / 5 / 1 | 1 → 1 | 0 / 0 / 1 | 0 / 1 | 0,1,2,3,4 | 1,072 / 1,072 |
| RUIN.DEM | 3,014 | 1 / 5 / 1 | 4 → 3 | 0 / 0 / 1 | 0 / 3 | 1,0,2,3,4 | 1,492 / 1,492 |

All five load successfully. This verifies replay loading and data mappings;
it does not establish deterministic playback or original-binary gameplay
equivalence. Synthetic truncation, overflow and I/O-failure coverage remains.

No macOS, Linux or Dreamcast build/runtime was executed. No live matchmaking
or multiplayer test was performed.
