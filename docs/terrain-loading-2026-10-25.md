# Terrain-loading safety audit — 2026-10-25

Baseline: the clean `main` checkout at `ed76a5f`. The investigation and fix are
limited to the `.TER` loader and its published terrain tables.

## Confirmed reachability and evidence

Normal level initialization reaches `InitLevel` → the per-track initializer →
`LoadTrackData` → `LoadTerrain`. `InitLevel` subsequently calls
`ParseTerrainHeader` before initializing player spawns and cameras. Terrain
files come from the selected track's local `BIN/EXTRAS/*.TER` path.

`LoadTerrain` uses a 32-byte-aligned static array of 20,123 `uint32_t` values:
**80,492 bytes**. There is no terrain heap allocation. The baseline sought to
EOF and passed the unchecked signed `ftell` result to `fread` as its byte count.
It ignored seek, read and close failures. A larger readable file therefore
overwrites the fixed array; a failed length query can also become a huge
unsigned read request. Missing or incomplete files could leave old/partial
terrain for the later parser.

An **80,493-byte synthetic file reproduced `global-buffer-overflow`** under
Windows x64 MSVC AddressSanitizer. The report identifies `fread`, called by the
unchanged production `LoadTerrain`, writing immediately beyond
`s_terRawStorage` (size 80,492). The baseline source and raw diagnostic were
kept outside Git. No original file was modified to reproduce the problem.

The five supplied original PC terrain files fit the existing buffer:

| File | Bytes |
| --- | ---: |
| ISLAND.TER | 80,492 |
| CITY.TER | 46,396 |
| RUIN.TER | 64,164 |
| FACTORY.TER | 63,864 |
| EMERALD.TER | 30,112 |

The overflow requires an oversized/replaced local terrain file; it was not
observed with these original files. Terrain data is not supplied by network
packets in this loading path.

## Parsing and indexing assumptions

The 92-byte little-endian header contains ten section offsets, nine record
counts and four grid parameters. Consecutive offsets bound the sections;
record data may be followed by alignment padding. Original files contain
two-byte padding after some vertex, face, edge and loop tables, so exact
`count * stride == section size` checks would reject valid data.

| Section | Record size / consumer assumption |
| --- | --- |
| Vertices | 6 bytes; face entries contain unsigned vertex byte offsets |
| Faces | 14 bytes; surfaces select signed first-face/count ranges |
| Surfaces | 16 bytes; grid entries select the low 12 bits as an index |
| Collision meshes | 16 bytes; signed base plus byte vertex count selects edge vertices |
| Edge vertices | 6 bytes |
| Loop entries | 22 bytes; row bases/counts and forward/reverse surface ranges drive loop indexing |
| Loop rows | 34 bytes |
| Grid index | 1,024 signed short offsets into grid data, one per 32×32 cell |
| Grid data | Short entries; bit 14 selects walls, bit 15 terminates a list; first-entry `0xFFFF` is empty |
| Items | 12 bytes; signed indexes select collision meshes whose masks can be changed |

Previously the parser constructed all pointers without checking file length,
offset order, counts or embedded references. Big-endian fixups also used those
offsets/counts for in-place writes. Collision, camera and rendering callers
trust the published data. `UpdateTrackWorld_*` calls `AnimateTrackGeometry`
with fixed item groups: two on Island/Ruin/Emerald, three on City/Factory.
Ruin ground collision separately reads item records 0 and 1.

## Correction

The static storage, alignment, public void load/parse APIs, terrain structures
and original-address annotations remain in place. The loader now checks both
seeks, the signed length (92 through 80,492), the complete read, stream error
state and close result. It records a usable length only after successful I/O.

Before publishing metadata or swapping body bytes, the parser checks ordered,
aligned, in-file section offsets and bounded nonnegative counts. Division by
record size avoids unchecked count multiplication. It validates the embedded
face, surface, wall, loop, grid-list and item references, including list
termination and the fixed animation item groups. Grid cell dimensions must be
positive and their bounds representable by the original 12-bit fixed-point
position arithmetic. Little-endian header assembly now uses unsigned shifts,
preserving negative origin values without signed-shift undefined behavior.

Malformed or unreadable terrain prints an error and calls `exit(EXIT_FAILURE)`.
The existing `InitLevel` API has no asset-failure recovery path; returning
silently would allow initialization to continue with invalid terrain. This is
a failure exit, not a transactional buffer rollback or a new menu-recovery
mechanism. Existing registered exit handlers still apply.

Validation runs once per load before the existing fixups. It adds no heap
allocation or per-frame collision/rendering checks. Valid little-endian file
bytes and published values are unchanged. There are no save, replay, packet or
terrain-layout changes.

## Verification

Commands and harness details are in [tests/README.md](../tests/README.md#terrain-loader).

| Check | Result |
| --- | --- |
| Unchanged baseline, oversized synthetic file, MSVC ASan | Reproduced global-buffer overflow |
| Unchanged baseline, valid synthetic fixtures | 7 passed |
| Unchanged baseline, original PC terrain files read-only | 5 passed |
| Corrected loader, GCC | 2,500 synthetic cases passed |
| Corrected loader, GCC `-fanalyzer`, warnings as errors | 2,500 cases and 5 original files passed; no diagnostics |
| Corrected loader, MSVC ASan | 2,500 cases and 5 original files passed |
| Existing replay loader regressions | 257 passed |
| Existing settings regressions | 168 passed |
| Development/release incremental dependency scenarios | Passed |
| Fresh UCRT64 GCC Windows development build | Passed |

The terrain harness uses real stdio and synthetic temporary files, with
deterministic seek/tell/read/error/close failures. It checks every shorter
length of a representative 2,360-byte file, the exact 80,492-byte boundary,
oversized input, invalid metadata/references, invalid tracks and reloads.
Failed loads must close opened streams and avoid publishing invalid metadata.
Valid loads are independently compared against the file's offsets, all thirteen
published scalars, the collision-mesh extent and every loaded byte. Both the
baseline and correction pass these comparisons for all five original files.
Private file paths, assets and raw evidence are absent from this commit.

Big-endian and Dreamcast execution were not available; their fixup bounds were
reviewed in the shared source. Other asset loaders, cross-file AI/geometry
references and general physics/numerical validity are outside this focused
audit. Matching game assets and valid runtime player/camera indexes remain
assumptions.

## Corrected Windows gameplay verification — 2026-10-25

The development executable at `source/sdl/build/terrain-verification/sonicr.exe`
was confirmed up to date with the corrected source at `777d52e`. Its SHA-256 was:

```text
D3EB7BE2C13A153871060CE1BE592DFE556D3929F198C223459420D0D9374C2E
```

The game reused the existing isolated private runtime copy and was launched
with `--lan --unlock`. The existing unlock option made all five tracks available
for the smoke test. The user operated the game window and evaluated gameplay;
automated checks monitored the owned process and both diagnostic streams.

The verified runs entered through Time Attack and course selection. Each track
initializer below was followed by `InitLevel OK` and `MAIN GAME LOOP`:

| Track | Initializer | User-observed runtime result |
| --- | --- | --- |
| Resort Island | `InitIsland` | Rendered and played normally; no issues |
| Radical City | `InitCity` | Rendered and played normally; no issues |
| Regal Ruin | `InitRuin` | Rendered and played normally; no issues |
| Reactive Factory | `InitFactory` | Rendered and played normally; no issues |
| Radiant Emerald | `InitEmerald` | Rendered and played normally; no issues |

The user confirmed that all five tracks rendered and played normally, with no
issues. The complete captured diagnostics contained no terrain-loading failures
or crash/error messages. The process was responsive at monitoring checkpoints
and exited with code 0 after the owned test window was closed normally.

All 496 original runtime file paths, sizes and SHA-256 hashes were unchanged
after the test. The five prepared `.TER` files still matched their originals.
Raw logs, process records and hash manifests remained outside Git.

Together with the regression, sanitizer and original-file compatibility checks
above, these results complete verification of the confirmed terrain-loading
buffer-overflow fix for the current Windows PC target. The issue is considered
closed in that scope. This was a short gameplay smoke test across all five
tracks; the broader assumptions listed above remain outside its coverage.
