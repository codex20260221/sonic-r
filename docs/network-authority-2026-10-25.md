# Network sender authority and host resilience — 2026-10-25

The baseline is `c75c79a` on `main`, after the terrain runtime verification.
Automated tests use synthetic fixtures and local loopback only. The later
Windows gameplay check below uses two instances on the same computer.
Original assets, private configuration and raw diagnostic evidence are outside
Git. Existing receive/loss regression work is retained.

## Findings established against the baseline

The original transport identified a peer by IPv4 address and UDP source port.
An unknown sender was immediately entered into a free slot, regardless of
packet type. The dispatcher then treated a non-JOIN packet from an unknown
application slot as a fatal programming error and called `abort()`.

Established clients' gameplay packets carried an independent player index.
The dispatcher checked that index against the array bounds and active flags,
but did not require it to match the transport's sender slot. A valid body
therefore allowed one client to update another slot, including the host.
The client receive path also lacked a consistent host-only sender check.

The receive thread queued only a slot number. After the main thread freed
that slot and the receive thread reserved it for another endpoint, an old
queued packet could be interpreted as belonging to the replacement peer.

Tests against the preserved, unmodified baseline establish these failures:

| Reproduction | Baseline observation |
| --- | --- |
| `input` | Slot 1 input attributed to slot 0 changes receive/input state; both input-only and full-state forms are covered by the repaired suite. |
| `snapshot` | A snapshot from a sender other than the configured host changes client state. |
| `control` | A client's otherwise valid lobby configuration changes the host's config. An unrelated sender's GO also changes the client's loading flag. |
| `malformed` | A five-byte START_ACK changes the host's ACK mask. |
| `fields` | Invalid START metadata is partially published instead of rejected. |
| `crash` | An unexpected four-byte first datagram from an unknown loopback endpoint aborts the host. |
| `stale` | An old queued JOIN is applied after slot reuse and authorizes the replacement endpoint using the previous peer's identity. |
| `wait` | An overlong legacy frame broadcast is accepted by `WaitForNetworkData()`. |
| `barrier` | A legitimate LEAVE during the host loading barrier does not release the wait; the harness times out after 20 seconds. |

Core valid input/snapshot cases and the existing 615 receive cases passed on
the baseline. The existing loss suite also passed before these changes.
These authority failures are separate from the previously repaired framing,
transactional decoding and packet-loss recovery defects.

## Corrections

Unknown endpoints can reserve a free slot only with a recognized, complete
JOIN request while the host lobby is accepting joins. Reservations remain
pending until application validation commits them; pending endpoints cannot
send gameplay or other controls. Admission closes when race initialization
begins. Established peers continue using their existing slots.

Each endpoint/session association has a local generation stamp. The receive
thread queues that stamp with the packet, and dispatch drops packets whose
association has since changed. Registry, admission and generation changes
are synchronized with transport sends and receives. This stamp is local
metadata; it does not appear on the wire.

The dispatcher requires an established client endpoint for client messages,
and the configured host endpoint for server messages. Client input, character
changes, legacy pad data and keepalives must claim the sending endpoint's own
slot. Host relays and snapshots retain their authority to update remote slots
on clients. The client's own physics remain authoritative as before.

Control messages accept only their supported lengths. START validates its
player count, local assignment, configuration indices and active character
IDs before publishing session state. A duplicate valid START re-ACKs without
resetting a running race. Its configuration is copied through the existing
address aliases so it remains authoritative if a preceding lobby broadcast
was lost or reordered. Clients cannot supply the host's lobby configuration.

A valid LEAVE releases only its established sender's slot, clears that slot's
active/receive state, and preserves the remaining peers. The loading barrier
recomputes its expected mask after a leave. The remote-triggered fatal check
is removed; the local send-before-slot programming assertion remains.

No encoder, packet header, protocol version, field width, sequence rule,
keyframe cadence, original-address annotation or game-data layout changes.
No per-packet allocation is added; the new association stamp and fixed-size
receive buffer are bounded. This audit does not include a performance benchmark.

Supported legacy forms remain accepted: JOIN requests of 37, 54 and 56 bytes;
SLOT_ASSIGN of 140 and 208 bytes; PEER_NAME of 41 and 58 bytes; START of 8, 16
and 24 bytes; version-0 gameplay markers and input-only updates. Unspecified
trailing bytes and partial control extensions are now rejected. Current
senders do not emit those forms. Older receivers remain vulnerable until
updated; unchanged wire formats do not fix an unpatched peer.

## Verification

The new harness compiles the complete production receiver and UDP transport,
uses the production codec/interpolation code, and reuses the transactional
receive fixture. Deterministic rejection checks compare player state,
histories, sequences, input/receive flags, timestamps, interpolation, finish
bookkeeping, identity tables, configuration, control masks and transport
associations. Invalid packets are tested alongside accepted traffic.

Socket tests bind only to loopback and use OS-assigned ports. They exercise
JOIN/discovery, slot assignment, two-client host startup, ACK/READY/GO,
gameplay, invalid/unknown senders, clean leaves and client shutdown. The host
lifecycle is exercised both synchronously and with the real receive thread.
Slot-reuse ordering is scheduled explicitly for a deterministic regression.
The standalone barrier and legacy wait checks detect blocked or prematurely
accepted controls. Watchdog tests verify the existing five-second boundary,
host-loss termination and isolation of a timed-out peer from a surviving peer.

| Check | Observed result |
| --- | --- |
| GCC authority suite | 626 cases pass |
| GCC authority suite with `-fanalyzer`, warnings as errors | 626 cases pass; no analyzer diagnostics |
| Native MSVC authority suite with AddressSanitizer | 626 cases pass; no sanitizer diagnostics |
| Existing packet receive suite | 615 cases pass |
| Existing delta codec suite | 140 cases pass |
| Existing loss/reordering suite | 96 scenarios, 8,822 deliveries; no unsafe or incorrect commits |
| Loss-suite sender fingerprint | Unchanged: `066e265228162286` |
| Existing interpolation suite | 5 tests pass |
| Windows UCRT64 development build | Fresh build and final incremental rebuild succeed |

Commands are in [tests/README.md](../tests/README.md). For the Windows build:

```sh
cd source/sdl
make -j8 CC=gcc BUILDDIR=build/authority-verification TARGET=build/authority-verification/sonicr.exe
```

To export the baseline without changing the checkout, use PowerShell and
native Python; the destination is outside the repository:

```powershell
$env:SONICR_AUDIT_BASELINE_DIR = Join-Path $env:TEMP 'sonicr-authority-baseline'
python -c 'import os, subprocess; from pathlib import Path; p=Path(os.environ["SONICR_AUDIT_BASELINE_DIR"]); p.mkdir(parents=True, exist_ok=True); [(p/n).write_bytes(subprocess.check_output(["git", "show", "c75c79a:source/sdl/src/net/"+n])) for n in ("network.c", "net_transport.c")]'
```

With that variable inherited by UCRT64, select one of the failing baseline
groups listed above (each command deliberately exits nonzero):

```sh
python tests/run_net_authority.py --cc gcc --source "$SONICR_AUDIT_BASELINE_DIR/network.c" --transport-source "$SONICR_AUDIT_BASELINE_DIR/net_transport.c" --case input
```

## Invariant status and remaining risks

| Invariant | Status after correction |
| --- | --- |
| Client updates affect only the established sender's slot | Pass in deterministic and loopback cases, including the host-slot boundary |
| Unknown endpoints cannot change established player state | Pass for tested invalid controls/gameplay, malformed JOINs and in-race JOINs |
| Invalid/unauthorized controls cannot terminate the legitimate host | Pass for the tested controls and unexpected first datagram; no receive-path abort remains |
| Valid host/client traffic and normal disconnects work | Pass in fixtures, real loopback lifecycles and watchdog tests |

The reproduced authority, fatal-error and malformed-control defects are
resolved within this scope. The following limits are separate from those
confirmed defects:

- Endpoint association is not cryptographic authentication. Open lobbies
  still admit well-formed JOINs without a secret or identity proof. Source
  address/port spoofing, on-path interference and lobby capacity exhaustion
  are not tested or prevented by this change.
- There is no wire session nonce. Local queued packets from prior associations
  are rejected, but a delayed wire packet arriving from exactly the same
  endpoint after reconnection cannot be distinguished solely by the local
  generation. Cross-session replay protection remains unverified.
- A peer still supplies its own physics, animation and race fields. Semantic
  bounds and downstream use of hostile but well-framed own-slot values need
  a separate audit; no downstream crash is claimed here. This is the next
  focused correctness/safety investigation recommended for receive data.
- The existing level-ready wait has no general deadline for an established
  peer that remains silent without sending LEAVE. This audit proves the leave
  case, not resilience to a peer that never finishes loading. Queue saturation,
  rate limiting, exhaustive concurrency interleavings and thread sanitization
  also remain unverified.
- MSVC supplies AddressSanitizer coverage, not UBSan or ThreadSanitizer.
  Other operating systems and Dreamcast were not built or run. The automated
  audit did not exercise external peers or matchmaking. Subsequent Windows
  gameplay results and their limitations are recorded below.

## Live Windows verification of `c531071`

The checkout was clean on `main` at `c531071`. The UCRT64 development executable
was current according to Make; both launcher-created copies matched its
SHA-256. The existing `tests/windows_lan.ps1` setup reused separate private
runtime copies and launched both instances with `--lan`. The user operated
the windows while process status and paired development logs were monitored.

The completed race used this computer's own LAN-interface address. Subsequent
lobby and departure/rejoin checks used direct `127.0.0.1`. All participating
game processes were on the same computer; no remote peer or public matchmaking
was involved. Completed-race coverage is for the local LAN address; direct
loopback coverage in this run is for lobby and departure/rejoin behavior.

| Check | Observed result |
| --- | --- |
| Host creation and client joining | Pass; two-player connection and slot 1 assignment confirmed in paired logs |
| Race initiation and synchronization | Pass for the completed race; START ACK mask `0x3`, expected `0x3`, one retry; READY/GO and game-loop entry confirmed |
| Independent movement in both directions | User confirmed both directions worked as expected |
| Complete multiplayer race | User completed Resort Island with Sonic and Tails; gameplay worked as expected |
| Normal race-session closure | Both logs recorded session closure after the completed race; both processes remained responsive |
| Client departure and rejoin without restarting the host | Pass twice in the direct-loopback lobby; host received LEAVE, released slot 1, then reused slot 1 for the rejoining client, with no host close/recreation between those events |
| Host idle timeout, lobby recreation and another join | Fail; duplicate client entries and a subsequent black-screen startup stall, detailed below |
| Test-process shutdown | Both owned windows closed through the launcher's normal close request and exited with code 0; no forced termination was needed |

The user corrected the initial name report: on the host, the joining client's
name was present (the underscore in the synthetic name displayed as an `a`),
while the host's own name was blank. The joining screen showed no names until
the host started; Windows platform icons were visible. Paired logs contained
the intended raw names. This lobby display issue remains unresolved. Source
review finds that the client lobby player count is not populated by SLOT_ASSIGN
and the local name uses a separate portrait buffer; those paths predate the
authority patch. No baseline GUI comparison was performed, so this is not a
claim that every part of the display problem has been isolated.

### Confirmed idle-timeout lifecycle failure

After successful departure/rejoin, the host lobby returned to the title screen
while the client remained waiting. The user re-established the host lobby,
left on the client, and joined again. The host then displayed itself and two
copies of the client. Starting this session left both windows black.

The host log establishes the state mismatch:

- The subsequent client was assigned slot 2 and `playerCount=3`.
- START acknowledgement stopped at `0x5`, with expected mask `0x7` and 50 retries.
- The level barrier expected `0x7`; slot 2 repeatedly reported ready, leaving
  the mask at `0x5`. Slot 1 was an absent participant retained by the host.

Both processes continued responding to window events. Neither log emitted a
fatal error, crash, disconnect or timeout diagnostic for this stall. Process
liveness and clean logs therefore did not establish successful startup.

The source trace explains how the stale slot can survive: `NetworkScreen()`
and `NetworkScreenReentry()` implement a 180-second idle return to the title
without the `CloseDirectPlaySession()` call used by Esc. Lobby entry resets
`g_netPlayerCount`, but `net_host_start()` returns success for an already active
host without clearing its peer table. A fresh client endpoint can then occupy
another slot. START and the loading barrier wait for the retained endpoint too.
The relevant timeout/menu paths predate `c531071`; this run establishes the
failure in `c531071`, without an older-build gameplay comparison.

At this verification checkpoint, session-lifecycle resilience was **not closed**.
The immediate follow-up was to close abandoned lobby sessions consistently, preserve the successful
lobby-to-race handoff, and add a timeout/re-entry regression before repeating
this live scenario. The clean LEAVE/rejoin path and normal completed race are
verified separately from this failed path. No production fix was made during
this verification pass.

### Original-runtime integrity and verification limits

All 496 original runtime file paths, lengths, SHA-256 hashes and write timestamps
matched the pre-launch manifest afterward: no added, removed or changed files.
The total remained 638,606,097 bytes. Only the prepared copies were writable
game directories. Raw logs, process samples, launch metadata and hash manifests
remain outside Git. Both test processes were stopped and their UDP endpoints
were released.

No synthetic regression suite was repeated in this documentation pass. The
previous automated results above remain the evidence for packet rejection and
authority checks; this live run used legitimate traffic and does not prove
spoofing resistance or downstream field safety. The Windows build currency
check passed. No game assets or production source were modified.

## Lifecycle correction after live verification

The correction starts from `b3c7945`, with the same production implementation
as the failed live run. The first lifecycle tests were executed against that
unchanged implementation before any production changes.

### Deterministic reproduction and state ownership

The fixture executes the complete production `NetworkScreen()` and
`NetworkScreenReentry()` loops, the session API in `main.c`, and the real
transport/receiver. Only platform input/time, asset loading, graphics/audio and
online service boundaries are replaced. All UDP binds and sends are loopback;
the session API's fixed port is redirected to OS-assigned test ports.

Baseline observations were:

- A client timeout returned `SCREEN_TITLE` with the transport still active.
- Host timeout followed by actual menu recreation and a new client endpoint
  produced `slot=2`, `players=3`, expected readiness `0x7`, actual `0x5`.
- Explicit close left ACK mask `0xf`, READY mask `0xf`, GO set, and a queued
  JOIN present. Esc and successful race handoff cases passed on the baseline.

The primary defect was a split lifecycle: the idle fade returned from the
lobby without closing its socket or receiver thread. Endpoint registrations,
their generations and the open lobby's admission state survived. Re-entry
reset `g_netPlayerCount`; `net_host_start()` correctly reused its already active
listener, but that listener still owned the old client slot. A new endpoint
then received slot 2. START reset its ACK mask but expected all three slots,
including the retained participant; loading could not complete.

The teardown test also established that stopping the transport alone did not
clear application readiness/queued control state. Some roster data must survive
ordinary race closure because `main.c` closes the network before showing the
results screen. Clearing every player/name/count field at close would change
that existing behavior.

### Correction and compatibility

- Both lobby entry points call `NetLobbyReset()`. It retires any previous
  transport and receiver, then clears local lobby identities and the player
  count before starting a new lobby. Saved selections and original data are
  preserved.
- Both common fade-return paths close the session for results other than
  `SCREEN_OK`, covering idle timeout and cancellation. A successful handoff
  keeps its listener, peers, receiver and session state for the race.
- `CloseDirectPlaySession()` stops and joins the receiver before clearing the
  packet queue, closes the game/discovery sockets, invalidates endpoint
  associations, and resets readiness, ACK/GO flags, pending lobby controls,
  receive/input flags, timestamps, expected-player count and codec/interpolation
  history. Repeating close is safe. An assigned client still sends its existing
  best-effort LEAVE before teardown.
- Player data and the results roster remain available after race closure;
  the next lobby clears those identity caches. The roster preservation check
  compares player bytes, names, decorations and player count across close.

The transport's idempotent start behavior, packet formats, protocol version,
normal LEAVE/rejoin, and the original 180-second idle policy are unchanged.
No decoded physics/animation code or name-rendering behavior was modified.
The blank local host name and delayed lobby names remain separate issues.

### Verification and live retest status

| Check | Result |
| --- | --- |
| Lifecycle suite, GCC | 36 cases pass |
| Lifecycle suite, native MSVC AddressSanitizer | 36 cases pass; no sanitizer diagnostics |
| Repeated timeout/recreation/start | Eight sequences pass; new client always slot 1, player count 2, ACK/READY masks `0x3`, GO received |
| Resource lifetime | Twelve cycles plus warm-up; zero Windows OS handle growth, receiver stopped, sockets closed, slots freed |
| Old queued JOIN / obsolete controls | Rejected or cleared across recreation; old generation cannot authorize the new slot owner |
| Results roster and successful handoff | Preserved in both production lobby loops |
| Existing authority suite, GCC and native MSVC ASan | 626 cases pass on each |
| GCC authority suite with `-fanalyzer` | Pass; no analyzer diagnostics |
| Existing receive / codec / interpolation | 615 / 140 / 5 pass |
| Existing loss/reordering | 96 scenarios, 8,822 deliveries pass; fingerprint remains `066e265228162286` |
| Fresh Windows development build | Pass in `build/lifecycle-verification` |

Commands and fixture boundaries are in [tests/README.md](../tests/README.md#network-session-lifecycle).
To reproduce the pre-fix failures, export `net/network.c`, `net/net_transport.c`,
`screen_misc.c` and `main.c` from `b3c7945` into an external directory, then pass
their paths through the four source options. `--case recreate` and `--case reset`
deliberately fail on that baseline without game assets or a wall-clock idle wait.

The deterministic defect is corrected. A live retest with the corrected Windows
executable is the remaining verification step: clean leave/rejoin, host idle
timeout, host recreation, client rejoin as slot 1 with exactly two players,
then READY/GO and a complete race. Runtime results will be recorded separately
from the synthetic cases.

The current protocol has no host-shutdown control or session nonce. A client
can remain in its waiting lobby until its own timeout or an explicit leave;
this correction makes those exits release its local resources. Arbitrarily
delayed wire packets from an identical reused endpoint, and an established
peer that never reports level-ready, remain separate limitations. General
loading-barrier deadlines, full leak analysis and exhaustive thread interleavings
are not claimed. Other operating systems and Dreamcast were not run.

## Candidates for the downstream decoded-field audit

After the lobby lifecycle defect is addressed, these are the most promising
focused entry points. They are investigation leads, not newly reproduced
vulnerabilities; no changes or hostile-value runtime tests were made here.

1. **Coordinates and interpolation arithmetic.** Trace decoded positions through
   `net_delta_apply()`, `NetInterpRecord()`, `lerp_int()` in `net/net_interp.c`,
   and `DeriveGroundState()` in `ground_collision.c`. Coordinate differences
   are formed before widening in interpolation, and terrain/projection callers
   perform further signed arithmetic. Existing grid bounds checks must be
   retained. Start with synthetic extreme and valid coordinate pairs and verify
   reachability and original integer semantics before selecting a correction.
2. **Animation transitions and cursor validity.** `main.c` calls
   `TickPlayerAnimation()` for remote players even when their physics is
   skipped. Review decoded `animId`, `abilityState`, grounded/vehicle flags,
   forward/lateral speed and their interactions with `prevAnimId`, character
   changes and `g_animDataPtrs`. The final animation lookup already checks its
   index; the promising remaining areas are blend-pointer arithmetic, cursor
   reseeding, stream sentinels and speed-dependent frame advancement. Synthetic
   animation tables can exercise these without original assets.
3. **Item/effect rendering and state ranges.** `itemEffectId`, `itemEffectState`,
   `itemHeightMod` and `effectYMod` reach `render_character_d3d.c` and
   `RenderItemSprite()` / `RenderPlayerItemEffect()` in `render_char_sprites.c`.
   Check UV-table indices, projection arithmetic and legal state combinations
   using synthetic lookup tables and captured draw arguments. Rendering remains
   reachable for remote players despite the physics skip.
4. **Race progress and finish bookkeeping.** Review decoded `lapsCompleted`
   and `racePosition` with position-derived progress in `race.c` and finish
   transitions in `net/network.c`. Establish behavior for discontinuities,
   repeated finish transitions and extreme values without changing valid race
   ordering. Physics-only consumers of other state fields should first be
   checked against `net_should_run_physics()` before being treated as reachable.
