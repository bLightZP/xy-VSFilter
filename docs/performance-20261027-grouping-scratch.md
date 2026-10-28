# Native grouping scratch storage, 2026-10-27

This batch targets the remaining frame assembly paths in the native renderer.
The console uses one render thread, CPU 7, affinity 0x80 and verified Above
Normal priority. Builds use that CPU at Normal priority and run separately.
No playback, filter registration or player-setting changes are performed.
Controls are the frozen font/style/layout builds from the preceding batch.

## Evidence and implementation

A fresh x64 console profile collected 3,665 samples, 3,642 with stacks and zero
lost events. CompositeDrawItem::Draw appeared in 1,953 inclusive samples;
CreateDrawItemExTree in 167, including array construction/allocation and initial
membership allocation. Inclusive counts overlap. DrawSingleColorPackPix still
accounts for 454 exclusive samples, so blending remains a separate target.

CompositeDrawItem metadata now occupies one exactly sized array per Draw call.
A counting pass precedes allocation; event views retain each event's original
range. The array never grows after pointers are published. The view records
contain only a pointer and count, so the outer ATL array can relocate them.
Shadow, outline and body traversal still happens separately within each event.

Each initial rectangle uses a membership list embedded in its metadata item.
Merged rectangle memberships use a frame-owned ATL node pool with blocks of
64 headers. Only used nodes construct lists; spills still allocate according
to actual membership size. Pointers remain stable as new bands are discovered,
and rectangle copies preserve the existing aliasing of membership lists.
Scratch storage remains alive through grouping and drawing, then is destroyed.
Completed frames and cached keys continue owning their draw/bitmap data and
retain no pointer to this scratch storage. No cross-frame arena or new worker
thread is introduced.

The rectangle splitting/merging algorithm, the 128-item draw-method threshold,
empty slots, inverse-clip regions, ordering and pixel arithmetic are unchanged.
Both DirectVobSub's SubPicDesc adapter and XySubFilter's provider reach the
shared native RenderEx and CompositeDrawItem::Draw implementation. x64 retains
owned group-key snapshots; Win32 retains borrowed lookup probes.

## Measurements

The flat-metadata-only prototype reduced tree time 14.10%, with full-window wall
time 0.09% higher and CPU time 0.86% higher. It did not establish an end-to-end
gain alone. The final combination adds frame-owned membership storage.

Medians of complete pass totals, with four passes per x64 build across two
ABBA batches and two passes per Win32 build:

| Metric | Control (s) | Candidate (s) | Reduction |
| --- | ---: | ---: | ---: |
| x64 full-render thread CPU | 34.750 | 33.703 | 3.01% |
| x64 full-render wall | 34.915 | 33.879 | 2.97% |
| x64 composition phase within full render | 19.622 | 18.479 | 5.83% |
| x64 tree phase | 1.674 | 1.096 | 34.53% |
| Win32 full-render thread CPU | 29.883 | 28.867 | 3.40% |
| Win32 full-render wall | 29.909 | 28.951 | 3.20% |
| Win32 composition phase within full render | 16.115 | 15.084 | 6.40% |
| Win32 tree phase | 1.313 | 0.686 | 47.74% |

Architectures were measured at different times; compare each to its own
contemporary control. Do not use this table as an x86-versus-x64 speed ranking.

The batches differ: total CPU improves 4.07% in the first and regresses 0.79%
in the repeat; the repeat also slows unchanged parsing, draw construction and
bitmap work. Tree and composition improve in both. Treat the overall gain as
variable; affinity and priority cannot remove frequency/PC activity effects.

Separate composition-only replay has two passes per build: median CPU
19.453125 -> 18.320313 seconds (5.82% lower), wall 19.364593 -> 18.219200
seconds (5.91% lower). Preparation is outside that timer and still executes
before each frame; this result isolates grouping/drawing, not complete rendering.
Nested phase times must not be added.

Across the x64 render passes, median per-pass p99 is 65.183 -> 62.365 ms
and maximum 142.153 -> 131.241 ms; p95 is nearly flat at 33.084 -> 32.995 ms.
Tail times still exceed the source frame budget of 41.708 ms. Median peak
process commit is 421.352 -> 426.186 MiB (+4.834 MiB); this is a broad
process/cache/heap metric, not the byte size of the scratch containers.
Renderer cache misses and bitmap work counts match. Overlay hit-only query
counts vary by six among approximately 4.81 million probes; control passes
themselves vary by four. These are not extra geometry/bitmap misses.

The fixed 17-frame new-event burst cohort is essentially flat on x64
(1,543.231 -> 1,541.744 ms total render time, 0.10% lower). Parsing itself is
unchanged and remains a spike source. Counting source-budget exceedances from
the per-frame medians gives x64 46 -> 49 and Win32 39 -> 39; these offline
exceedances are not playback dropped-frame counts. This batch does not establish
zero dropped frames or a consistent reduction in that exceedance count.

## Validation and compiled artifacts

Both Release/v145 architectures passed:

- All 21 focused contracts, both actual filter option/Main-page factory checks
  and all 54 fixture cases.
- All 12 extended ASS scripts, each at all 2,428 timestamps: reconstruction,
  zero alpha, polygon font fields, geometry reuse, multiline/inverse clips,
  omitted numbers, blur/karaoke, grouping stress, draw-slot spill, command
  prefixes and the Unicode/ANSI font/layout fixtures.
- The complete sample at 256 and zero bitmap-cache entries and complete
  composition replay, compared with the original pixel golden. The x64
  diagnostic-enabled pass also matches. Every timed comparison passed its
  frozen-control pixel gate.
- Direct x86/x64 full-sample comparison: zero mismatches in all 2,428 frames.

Comparisons cover bitmap counts, positions, dimensions and visible bytes.
Bitmap IDs and pitch padding are excluded. All comparisons report zero
mismatches. Retained-frame, cache ownership, inverse-clip and large-event gates
remain intact.

VSFilter, XySubFilter, xy_sub_filter_consumer and the console app were rebuilt
for both architectures. All six DLL versions remain 3.2.1.900. Normal outputs
under `bin/lib_18.0/<architecture>/Release/` byte-match their frozen stable
snapshots; source hashes, PDB hashes and PE architecture are verified. All 12
build logs pass the compiler/link/debug-data failure checks.

See [portable measurements and artifact hashes](benchmarks/20261027-grouping-scratch.json).
The local verification manifest is
`benchmark-results/remaining-20261027/verification.json`. No player was launched,
no filter registration was changed and no software was installed.

## Next work

Grouping still creates rectangle-band lists and performs the same band sweep.
A bounded reuse policy would need exact membership/bounds dependencies and
must include preparation/startup cost in its measurements. Do not reuse any
scratch pointer in a retained frame or cached key.

The fresh profile prioritizes mask/blend traffic for a future isolated kernel
experiment. Add packed and planar differential coverage for opacity, border,
short/misaligned rows, karaoke and both alpha conventions before fusing code.
Active ordering/expiry and conservative layout dependency reuse remain pending;
this batch does not test or exhaust every approach in the original review.
Console render time does not establish playback dropped-frame counts.

Local raw runs, profiler trace, binaries, PDBs and source snapshots are under
`benchmark-results/remaining-20261027/`. The extracted ASS and fonts remain local.
