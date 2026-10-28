# Compact grouping follow-up

Date: 2026-10-26. Work continues from commit `52768c30` at the user's request.
Subsequent authorization: the user requested libass integration and offline
single-CPU console benchmarking at Above Normal priority. Builds and native
checks have resumed under that instruction; playback remains paused. The text
below preserves the earlier checkpoint. New results are in the [libass/console report](performance-20261026-libass-native.md).

Earlier checkpoint: the user requested code-only work while using the PC. Do not run builds,
native tests, playback, profiling or benchmark analysis until the user makes the
PC available again. The compiled compact-storage candidate completed its native
validation before that request. Later borrowed-group-key edits are source-only:
they have not been compiled or tested.

## Fresh x64 evidence

The current player/helper executables match the successful Debug MAX build
started 2026-10-26 14:35. Its record is
`benchmark-results/zoom-debug-next-20261026.json`. The first setup attempt used
the older record, failed before playback/profiling, restored its temporary
settings and is excluded. No software was installed by this task.

Session 85 then profiled the actual native x64 helper using the committed
XySubFilter DLL. The 25-65 second interval has 2,730 CPU samples, all with stacks,
and zero lost events. All 1,897 sampled XySubFilter instructions resolve to the
workspace DLL; an installed DLL is also loaded but receives no sampled
instructions. Artifacts are under
`benchmark-results/next-20261026-current-x64-profiled-02`.

Selected sample shares (inclusive shares overlap):

| Function | Exclusive % | Inclusive % |
| --- | ---: | ---: |
| DrawSingleColorPackPix | 9.231 | 9.231 |
| CompositeDrawItem::Draw | 1.245 | 49.451 |
| CreateDrawItemExTree | 0.952 | 6.117 |
| MergeRects | 1.502 | 4.835 |
| GroupedDrawItems::CreateHashKey | 3.040 | 3.150 |
| malloc base | 0.403 | 8.608 |
| free base | 0.147 | 5.421 |
| qsort | 0.110 | 0.183 |

This prioritizes review item 2 (temporary grouping storage) over item 3 (event
ordering/expiry) for this iteration. Rendering still uses the same synchronous
shared RTS path for VSFilter and XySubFilter.

## Optional measurements

Native `--diagnostics` adds per-frame phase/work columns to the normal CSV.
Collection is disabled by default in the DLL, changes no COM API, and adds no
retained image references. Phase timers cover parsing/evaluation, draw assembly,
rectangle-tree/decide work, merging, group assembly and bitmap work. Composite
and render-core totals include their nested phases; do not add overlapping
columns. Residual time includes cleanup and other unlabelled work.

Work counters record input composite items, final groups, temporary membership
references, unique memberships, emitted layer draws and capacity changes during
final draw append. They count work, not total heap allocations. Reservation
allocations happen before append and are not counted as append growth.

A scoped serial native-harness probe records `xy_malloc` calls, frees, requested
bytes, failures and peak live bytes above each frame's starting value. It does
not measure ATL/STL/shared-pointer allocations and must not be presented as total
heap accounting. It is restored after RenderEx, before hashing/output work.
The allocation-probe contract checks calls, byte/peak accounting, release and
nested restoration. Normal playback never enables this probe.

The diagnostic-only control builds are frozen for both architectures in
`benchmark-results/next-20261026-diagnostics-control-<arch>`. The x64 sample
passes all 2,428 original-control pixel hashes with diagnostics enabled. It
records 3,823,822 input items, 28,827,127 temporary membership references,
15,835,700 unique memberships, 32,731,004 layer draws and **1,773,939 final draw
append growth events**. There are no xy_malloc failures.

## Candidate and isolated storage trial

The grouping algorithm, rectangle ordering, bitmap partitioning, clipping and
shadow/outline/body order are preserved. No invisible items are dropped and the
128-item draw-method threshold is unchanged.

- Per-item rectangle IDs use frame-owned compact storage with eight inline IDs
  and a vector spill. There is no dummy head/node allocation.
- Per-rectangle membership pointers use an inline-first contiguous sequence.
  Shared membership owners use a combined object/control-block allocation.
  Append preserves original membership order and duplicate references until
  the existing per-group deduplication point.
- That deduplication pass counts actual non-null layer draws. Final groups
  reserve their draw capacity once before ordered filling; keys/bitmaps retain
  their own existing ownership and cannot reference these transient arrays.

A first trial using one inline rectangle ID is frozen separately in
`benchmark-results/next-20261026-compact-one-inline-x64`. Its 2,428 sample hashes
match, and its append growth count is zero. One diagnostic pass reduces render
core time from 38.278 to 34.643 seconds (9.5%) and composite time from 22.608 to
18.821 seconds. These are single-pass diagnostic observations, not an accepted
playback speed-up. The eight-inline-ID variant subsequently passed the native gates below. Eight
is a conservative small-list choice, not an exhaustively tuned capacity. Its
single diagnostic pass has 35.542 seconds of render-core time and 18.753 seconds
of composite time. Group assembly is 1.344 seconds versus 3.133 in the control;
other phase times vary, so these observations do not establish an incremental
whole-frame benefit over the one-inline-ID trial.

`--grouping-contract` checks more than 128 composite items, overlaps, clipping,
pre-reserved appends, optional diagnostics and retained frames.
`fixtures/grouping-stress.ass` includes offscreen/transparent items, inverse
clips and a tall rectangle spanning more than eight output fragments. Its pixel
controls come from the preserved pre-change diagnostic executables. A clean final rebuild completed for all three DLLs and the harness on both
architectures. The x64 native harness still reported LNK4020 after a parallel
clean build. Rebuilding the affected library and linking the harness/DLLs
serially removed the warning. This follows the [Microsoft LNK4020 guidance](https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/linker-tools-warning-lnk4020?view=msvc-170);
the observed workaround does not establish the underlying cause.

## Completed native validation before the code-only restriction

The frozen compact-storage candidates are under
`benchmark-results/next-20261026-compact-final-Win32` and
`benchmark-results/next-20261026-compact-final-x64`. Both passed all 17 contracts,
54 small fixture cases, every one of the 2,428 sample frames at 256 and zero
bitmap entries, and all eight extended scripts. Every bitmap-position/size/RGBA
comparison has zero mismatches. The x64 diagnostic run is also pixel-identical,
with identical per-frame group/work counts and zero append capacity growth.

The diagnostic comparison is saved as
`benchmark-results/next-20261026/phase-comparison.json`. XY allocation failures
are zero. Phase/allocation observations apply only to those frozen sources;
they do not validate the newer group-key-view code below.

A subsequent x64 candidate profile (session 86) ended after about 25 seconds,
with completion `playback_stopped`, before a complete ending capture. It is
excluded from performance comparisons. The collector stopped successfully with
code 0; temporary COM registration, bitmap capacity and engine settings were
restored, and the test player/helper closed. The user then requested code-only
work. No partial trace analysis or further tests/builds/benchmarks were run after
that instruction. The prepared unprofiled ABBA sequence has not been run.

## Source-only group-key lookup follow-up

While testing is suspended, `GroupedDrawItems::Draw` now creates a private,
non-owning group-key lookup view over its already assembled child draw records.
Hash/equality walk the same ordered child keys and clip rectangle. A cache hit
therefore needs no temporary child-key vector or shared-pointer copies. On a
miss, the interner's existing value-copy path materializes owned children.
The existing extra interning lookup on a miss is not removed by this change.

Only `GroupedDrawItems` can construct a borrowed view. The public copy constructor
and copy assignment always materialize owned keys; assignment stages an owning
copy before swapping. The existing `CreateHashKey` API still returns an owning
snapshot. Views are valid only during synchronous lookup and must never escape
the frame. Cached keys still retain no parent word or frame-owned arrays.

`--group-view-contract` is newly written but **not run**. It covers materialized
cache misses, hit identity/storage reuse, owning copy/assignment, source-group
mutation, eviction, zero-capacity bypass, retained pixels and child/word
lifetimes. The existing inline-list contract gains inline/spill and self-swap
cases. The harness source now has 18 contract entry points; only the earlier
17-contract compact-storage build is validated. Current workspace sources no
longer match the frozen compact binaries because of these code-only edits.

## Next validation when the user releases the PC

1. Review and build the borrowed group-key change for x86/x64, using serial
   library/link steps if necessary to avoid the observed PDB warning.
2. Run all 18 contracts and all previous pixel gates. Compare hashes and group
   counts to the frozen compact-storage candidates, and explicitly test cached
   key ownership after frame mutation/destruction.
3. Measure the group-key change separately from compact membership storage.
   Optional `--diagnostics` and `summarize_diagnostics.py` provide per-frame
   work checks and phase totals; their allocator coverage is limited as above.
4. Run fresh unprofiled Debug controls/candidates with the current Debug record,
   default subtitle, same renderer/window/cache, 22:20 through EOF. The collector
   now includes MPCSubtitleHelper.ax in module metadata and dependency hashes.
   Revalidate executable hashes because Zoom Player changed during this work.
   Do not pool old player builds or the interrupted profile.
5. Verify settings restoration and preserve matching source/DLL/PDB manifests.

No end-to-end speed-up or zero-drop claim exists for this new grouping batch.
