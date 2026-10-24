# Replay loader safety contribution — 2026-10-24

`LoadReplayLog()` trusted file-controlled counts while writing into the fixed
65,536-byte `g_taGhostSource` array. One viewport with 32,769 frames requests
65,538 bytes. A synthetic file reproduced a Windows ASan
`global-buffer-overflow` in the unchanged production loader, immediately after
that array. Separately, a complete 30-byte header declaring three frames with
only five payload bytes was incorrectly reported as successfully loaded.

The fix checks a complete header, count relationships, supported metadata and
active character IDs, bounds the aggregate payload, and stages the complete
payload before changing game/replay state. It returns zero for malformed input,
allocation failure, short reads, stream errors, or close failure. The existing
`AutoSelectDemo()` caller already tries the next demo when loading returns zero.

## Baseline and repository inspection

- Working tree initially clean on `codex/verified-contributions-20261024`,
  commit `6dde823`, four commits beyond main. That branch remains unchanged.
- `origin` is `https://github.com/codex20260221/sonic-r.git`; `upstream` is
  `https://github.com/jnmartin84/sonic-r.git`.
- Both remotes' main refs were independently checked with `git ls-remote` at
  `3da98f2e134eac82e45cd69660216fa0e4563ac5`, matching local `origin/main` and
  `upstream/main`. This contribution branch,
  `codex/bounded-replay-loader-20261024`, starts directly at that commit.
- Read the supplied `AGENTS.md`, project README, desktop/Dreamcast guides,
  license, both Makefiles, prior contribution review and its test README.
- SDL and Dreamcast share `save.c`; Dreamcast substitutes its `fileio.h`
  wrappers. Their read/count/error/close contracts were checked in
  `source/dc/src/save_vmu.c`; demo paths use the normal game-data stream path.
- The GitHub connector's open-PR search returned the music-path PR #21 and
  no overlapping replay change; its replay issue search returned no results.
  Search coverage is not proof that no external work exists.

## Compatibility decisions and critical review

The disk format remains 15 little-endian 16-bit words followed by one contiguous
frame block per viewport. This count is established by the actual reader and
writer statements; the writer's existing “16-short” comment is inaccurate.
The original track 3/4 conversion, successful-load difficulty bookkeeping,
five character assignments with signed inactive values, zero-frame files,
trailing-data tolerance and untouched unused buffer tail are preserved.

The existing declarations specify 1–4 viewports/humans and 1–5 players. Normal
race setup and network setup were checked; network transport has four slots.
Validation permits humans up to viewports and viewports up to players. Only
active character IDs require the supported 0–9 range; inactive words retain
their legacy representation. Other metadata uses the existing enum domains,
including internal race type 4. No original demo files were available to
independently inspect their headers.

The declaration of `g_taGhostSource` now states its existing 0x8000-word extent
so the loader can derive capacity with `sizeof`. Its definition and storage size
are unchanged. The capacity division precedes multiplication; staging is at
most 64 KiB of temporary heap memory, avoiding a large Dreamcast stack frame.
All successful opens close exactly once, and all staging allocations are freed,
including injected read/error/close failures. Allocation failure is tested and
retains the prior state. `g_fileHandle` retains the engine's existing I/O
bookkeeping convention; it is excluded from game-state preservation claims.

The final diff was reviewed for integer bounds, zero-length handling, ownership
and cleanup, byte order, all header assignments, caller error handling and
platform-specific file wrappers. Existing original-address annotations remain.
No game physics, save/writer paths, assets, network packets, structure layouts,
licenses, dependencies or hosted CI are changed. The tests use production code
with forwarding I/O instrumentation, plus synthetic fixtures and link globals
for unrelated `save.c` entry points; they do not emulate the loader.

## Verification

Environment: Windows x64, OS version 10.0.26200.0, MSVC 19.44.35228.0,
Python 3.12.10. The baseline export was compared byte-for-byte with
`git show origin/main:source/sdl/src/save.c`. Commands are in
[tests/README.md](../tests/README.md).

| Check | Result |
| --- | --- |
| Baseline valid-format suite | 34 cases pass |
| Baseline complete header + short payload | Fails regression: `invalid replay reported success (35)` |
| Baseline 32,769-frame file, Windows ASan | Reproduced `global-buffer-overflow` in production `LoadReplayLog`; destination size 65,536 |
| Fixed loader, `python tests/run_replay.py` | 257 cases pass, warnings treated as errors |
| Fixed loader, `python tests/run_replay.py --sanitize` | 257 cases pass under Windows AddressSanitizer |
| Shared headers with `CFLAGS=/DSONICR_FLOAT32` | 257 cases pass; not a Dreamcast execution result |
| MSVC `/analyze` on production `save.c` | Pass, no diagnostics |
| `git diff --check` | Pass |

Prior findings were also independently checked:

| Prior finding | Independent Windows result |
| --- | --- |
| Interpolation slot ownership | All 5 patched tests pass normally and under ASan; baseline gives `expected 5000, got 1300`. Baseline uses only a no-op shim for its nonexistent reset API. |
| Truncated settings | All 168 patched cases pass normally and under ASan; baseline fails the deterministic four-byte fixture. |
| Header dependency tracking / GCC default-build warning | Production diffs reviewed. The build-dependency regression was attempted but stops with `WinError 2`: GNU Make is absent. GCC build claims were not independently reproduced here. |
| Truncated delta mutates history | Unchanged codec returns zero for 10 of 11 bytes while changing valid receive history from `(1000,2000)` to `(1100,2000)`. |
| Lost relative-delta predecessor | Unchanged codec decodes source position 1200 as 1100 after dropping the preceding 1100 update. |

MSVC checks used native compiler/linker options and a test-only compatibility
macro for the unused GNU-aligned extern. Unrelated link globals were provided
where MSVC required them. Raw local logs, exact baseline source and investigation
harnesses are retained under ignored `source/sdl/build/review-evidence/`.

## Remaining limitations and next contribution

GNU Make, MSYS2/MinGW and SDL development dependencies were unavailable. Full
Windows desktop development/release builds were not run. macOS, Linux,
KallistiOS/Dreamcast builds, big-endian execution, original/proprietary demo
files and live gameplay were unavailable; no runtime equivalence claim follows
from these synthetic checks. MSVC ASan supplies no UBSan or leak-check result;
the harness separately counts staging allocations and closes.

The next focused contribution should make delta decoding transactional: reject
a truncated packet without changing receive history or the destination player.
The failure is independently reproduced above, and that first fix can preserve
the current wire format. Packet-loss resynchronization and sender/slot authority
require separate protocol fixtures and compatibility review. Terrain loading
remains another high-priority safety lead from the earlier review; its format
and initialization failure path need independent validation with legitimate
terrain data before a defensible full fix.
