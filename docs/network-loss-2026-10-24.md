# Packet-loss recovery verification — 2026-10-24

Baseline: `5193ef1878bce38bbb7929bd8ad2506175892f8c` on the existing
`codex/windows-playable-20261024` branch. The working tree was initially clean,
and the 615 receive / 140 codec regressions passed before changes.

## Isolated production-logic delivery harness

`tests/run_net_loss.py` uses the existing receive runner and fixture. The C
harness includes the complete production `network.c` and `net_interp.c`, links
the production codec and game global definitions, and delivers scheduled
datagrams through `net_recv()` into `ApplyNetworkPlayerState()`. No packets
reach a real socket. This is deterministic production-logic verification,
not a live multiplayer session or a concurrency test.

Snapshot traces come from the actual `net_build_snapshot()`, including its
countdown. Client traces use the production encoder in the existing 12-byte
client envelope, with explicitly independent client sequences. An uninterrupted
production decode records authoritative expected state. Delivery schedules
drop, duplicate, reorder, retransmit, truncate and replay captured packets.
The expected dependency is the preceding sender packet, independent of the
receiver implementation. Every accepted history must match its authoritative
counterpart; rejected packets must preserve the transaction fixture's complete
history/player/interpolation/finish/input/sequence/version/frame/timestamp state.
Independent client streams additionally check that other clients do not change.

The full suite has 95 scenarios and 8,815 deliveries. It includes every one of
the 31 isolated-loss phases, initial/periodic/two consecutive lost keyframes,
bursts spanning keyframes, duplicates, reordering with and without a later
retransmission, stale/malformed packets, sequence wrap, three clients with
different sequences, legacy input-only packets, and partial resynchronization.

## Baseline failures and corrected results

Run commands and source-export instructions are in [tests/README.md](../tests/README.md).
The 26-scenario profile delivers 2,610 datagrams and deliberately exits nonzero
on the baseline. It reports **582 unsafe accepts and 638 incorrect accepted
states**. A state can remain wrong after a reordered predecessor is later
discarded as stale, which explains the different totals.

| Schedule, per stream | Baseline incorrect accepted states | Baseline maximum X error | Corrected incorrect accepted states | Corrected maximum accepted-update gap |
| --- | ---: | ---: | ---: | ---: |
| One lost delta | 29 | 100 | 0 | 31 sender ticks |
| Five lost deltas | 25 | 500 | 0 | 31 ticks |
| Reordered pair | 29 | 100 | 0 | 30 ticks |
| Reordered pair, then retransmission | 29 | 100 | 0 | 1 tick |
| Lost periodic keyframe | 30 | 100 | 0 | 32 ticks |
| Burst of 32 packets spanning a keyframe | 29 | 3,200 | 0 | 62 ticks |
| Two consecutive periodic keyframes lost | 60 | 200 | 0 | 63 ticks |

These measurements apply independently to snapshots and client input.
Both profiles produce the same captured sender-byte FNV-1a fingerprint:
`be4d482b83ea5fc7`. The encoder, field table, packet layout, protocol marker
and sender cadence are unchanged. Existing golden wire fixtures still pass.
Ignored exact outputs are under `source/sdl/build/loss-evidence/`.

## Receive-only recovery

A relative snapshot requires the next contiguous accepted snapshot sequence.
A relative client state requires the next sequence accounted for by that
client's baseline. Fresh keyframes can bridge a gap. Rejection changes no
history, player, interpolation sample, finish state or receive sequence.
There is no speculative application of an unproven delta and no new wire data.

Input freshness and baseline continuity are separate per-client counters.
An input-only packet can safely update absolute input bits after a gap, but
cannot pretend that a missing state packet was decoded. Contiguous input-only
packets account for an unchanged baseline, preserving that legacy form.
After a keyframe bridges a snapshot gap, only included slots have recovered:
omitted slots lose baseline validity without changing their players or saved
field values. A later delta cannot reuse their pre-loss histories.

No reorder buffer or retransmission request is added. A delayed predecessor
can still be accepted if it is the next required packet; already rejected
successors need redelivery or a subsequent keyframe. Snapshot state and the
three client streams retain separate sequence bookkeeping. Race/session reset
also resets the new input-baseline continuity counters.

## Recovery latency and compatibility

The existing countdown emits a keyframe every **31 packets**: one keyframe,
then 30 deltas. At the intended 30 Hz, 31 intervals are about **1.033 seconds**.
In the exhaustive phase tests, recovery occurs no later than 31 sender ticks
after the last lost/malformed packet, provided the next periodic keyframe is
delivered intact. An isolated delta loss in the displayed profile recovers in
30 ticks (1.000 s); losing the periodic keyframe requires 31 ticks (1.033 s).
The longest measured update gap with two lost periodic keyframes is 63 ticks
(2.100 s). These are sender-time measurements, not measured Windows LAN latency.

No finite wall-clock recovery can be promised under continuing keyframe loss
or arbitrary delay. The existing five-second disconnect watchdog remains.
Existing interpolation/extrapolation behavior is preserved: received authority
and delayed rendering samples are different observations.

This is wire-compatible receive policy, not a repair for old receivers:
unpatched clients can still misapply their own incoming deltas after loss.
No protocol version bump is needed, no bytes are added, and patched receivers
accept existing keyframes. Legacy senders that intentionally skip sequence
numbers without sending a packet will cause conservative waits for keyframes.
Desktop/Dreamcast mixed play has not been verified; shared source was retained
without new platform dependencies or changes to binary/save layouts.

## Validation so far

UCRT64 GCC 16.2.0 on Windows 11: all 95 loss scenarios / 8,815 deliveries pass,
including `CFLAGS=-fanalyzer` with warnings as errors and no diagnostics.
The existing 615 receive, 140 codec and 5 interpolation tests still pass.
This record describes isolated receive logic. Live Windows verification is
documented separately and must not be inferred from these results.
