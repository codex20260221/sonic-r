# Network sender authority and host resilience — 2026-10-25

The baseline is `c75c79a` on `main`, after the terrain runtime verification.
This audit uses synthetic fixtures and local loopback only. Original assets,
private configuration and raw diagnostic evidence are outside Git. Existing
receive/loss regression work is retained.

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
  Other operating systems and Dreamcast were not built or run. No external
  network, matchmaking service or new manual two-game race was exercised.
  The earlier Windows LAN gameplay observation predates this patch.
