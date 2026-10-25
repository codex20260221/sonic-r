# Transactional network receive — 2026-10-24

The baseline is `bc25d07` on `codex/windows-playable-20261024`, following the
[Windows integration verification](windows-integration-2026-10-24.md).
The codec fix is `c2008bedfa01591746eaac3fa794bf3f34a10b02`.
All work and commits stay in the independent fork. No matchmaking service,
remote push, PR, hosted CI or proprietary data is used in network tests.

## Reproduced defects

Deterministic tests against the unchanged production source demonstrate:

- A 9-byte prefix of a 16-byte multi-field delta returns zero but changes
  receive history. Four literal valid wire/missing-history cases still pass.
- A snapshot containing only its complete 12-byte header advances the receive
  sequence and session-frame/heartbeat bookkeeping despite missing entries.
- A snapshot with three complete remote entries and a truncated local entry
  publishes early player history/state and can assign a finish place before
  the last decode fails.
- Client input with an incomplete body advances its sequence/input bookkeeping.
  Even with the transactional codec linked to the old caller, failed decoding
  still records a new interpolation sample because the return value is ignored.
- Network slot 4 passed a `MAX_PLAYERS` (5) check before accessing arrays sized
  for `NET_MAX_PLAYERS` (4). The new malformed-slot checks exercise this boundary.

To reproduce without changing branches, export baseline files into ignored
build storage using native Python (PowerShell):

```powershell
python -c 'from pathlib import Path; import subprocess; p=Path("source/sdl/build/net-receive-evidence"); p.mkdir(parents=True,exist_ok=True); [(p/n).write_bytes(subprocess.check_output(["git","show","bc25d07:source/sdl/src/net/"+n])) for n in ("network.c","net_delta.c")]'
```

Then, in UCRT64 with native Python on PATH, these commands deliberately fail:

```sh
python tests/run_net_delta.py --cc gcc --source source/sdl/build/net-receive-evidence/net_delta.c --case atomic
python tests/run_net_receive.py --cc gcc --source source/sdl/build/net-receive-evidence/network.c --case snapshot
python tests/run_net_receive.py --cc gcc --source source/sdl/build/net-receive-evidence/network.c --delta-source source/sdl/build/net-receive-evidence/net_delta.c --case snapshot-late
python tests/run_net_receive.py --cc gcc --source source/sdl/build/net-receive-evidence/network.c --case input
python tests/run_net_receive.py --cc gcc --source source/sdl/build/net-receive-evidence/network.c --case decoder
```

The `decoder` reproduction intentionally uses the repaired codec with the old
caller to isolate its unchecked-return defect. No original game executable or
assets are needed for any reproduction.

## Implementation and compatibility

`net_delta_decode()` accumulates fields in a temporary history and publishes
only after all field reads succeed. Zero returns preserve both history and
destination. A NULL destination supports staging; `net_delta_apply()` publishes
validated fields while preserving all unrelated `Player` fields.

Snapshot decoding stages up to four histories (728 bytes for the pending
structure on this Windows build), validates every declared entry, and then
commits them in their original wire order. The local slot advances its receive
history without overwriting self-authoritative physics. Sequence, peer-version,
frame and heartbeat bookkeeping follow full validation. Finish transitions and
interpolation records happen only during the successful commit. A failed later
entry therefore cannot partially apply earlier entries. Lobby handling retains
its frame-only behavior after validation, without publishing race history.

Client input validates the body, checks its slot agrees with the header, and
checks the exact number of consumed bytes before publishing input, sequence,
receive flag, timestamp, version, history, player or interpolation/finish state.
The existing 12-byte input-only form is retained.

No encoder, field table, field width, packet header, keyframe cadence or protocol
version changes. Literal keyframe, relative-delta and overflow-submask fixtures
verify unchanged bytes. Version-0 markers and the existing sequence-wrap rule
remain accepted. Complete packets still apply the same player fields and finish
order. Packet validation now rejects out-of-range/duplicate snapshot slots,
oversized counts, header/body input slot disagreement, incomplete entries and
unconsumed trailing bytes. These are malformed forms the current senders do not
emit; they are rejected without partially advancing receive state.

## Verification

Windows 11, UCRT64 GCC 16.2.0, GNU Make 4.4.1, Python 3.12.10 and MSVC 2022
Build Tools were used. Reproducible commands are also in [tests](../tests/README.md).

| Check | Result |
| --- | --- |
| `python tests/run_net_delta.py --cc gcc` | 140 cases pass |
| `python tests/run_net_delta.py` in MSVC environment | 140 cases pass |
| `python tests/run_net_delta.py --sanitize` in MSVC environment | 140 cases pass under ASan |
| `python tests/run_net_receive.py --cc gcc` | 615 cases pass |
| `CFLAGS=-fanalyzer python tests/run_net_receive.py --cc gcc` | 615 cases pass; no analyzer diagnostics |
| Replay suite, UCRT64 and MSVC ASan | 257 synthetic + 5 authentic cases pass on each |
| Settings, UCRT64 | 168 cases pass |
| Interpolation, UCRT64 | 5 tests pass |
| Make dependencies, UCRT64 | Both development/release scenarios pass |

The analyzer initially reported an impossible uninitialized-slot path through
the snapshot staging helper. Explicitly zero-initializing the bounded staging
object removes that diagnostic; the full warning-as-error analyzer run passes.
UCRT64 has no `libasan` installed, so its packet tests have no ASan/UBSan result.
MSVC ASan coverage is for the codec and replay loader, not the packet dispatcher.
No macOS, Linux, Dreamcast, real network transport, threaded receive or actual
multiplayer session was tested. Passing these subsystem tests is not a claim
of full multiplayer compatibility or gameplay equivalence.

After the receive commit, build a fresh development executable from HEAD:

```sh
cd source/sdl
make -j8 CC=gcc BUILDDIR=build/integration-final TARGET=build/integration-final/sonicr.exe
```

## Remaining issue and next contribution

Lost relative-delta predecessors remain unresolved and are separate from
transactional rejection. An ignored asset-free diagnostic against the repaired
codec reproduces source position 1200 decoding as 1100 after dropping the 1100
update; the next keyframe restores 1200. It is retained at
`source/sdl/build/net-receive-evidence/lost_predecessor.c` with its executable.
This change does not infer missing deltas or add a resynchronization protocol.

The next priority is a deterministic loss/reordering transport harness and an
explicit compatibility design for recovery, followed by an actual local
multiplayer session with the same data/build. Sender/slot authority and terrain
loading safety also remain separate investigation leads.
