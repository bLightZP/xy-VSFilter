# Libass integration and standalone native benchmarks

Date: 2026-10-26. Base commit: `52768c30`. No playback tests were run and no
software was installed. The user authorized offline console tests on one
logical CPU at Above Normal priority while continuing to use the PC.

## Implemented behavior

Both filter Main pages have **Prefer libass rendering**, initially unchecked,
inside a Rendering group below Loading. The setting is persisted through the
existing options/registry mechanism without changing existing option IDs.

The shared renderer supports a libass backend for ASS/SSA. It preserves the
original script and embedded codec-private/event data, supplies attached fonts,
uses DirectWrite system font lookup, and composites libass's ordered alpha
masks into owned frames. Native remains selected by default and serves other
formats, including the extended embedded ASS2 format. Authored style edits and
Force Default style overrides are forwarded to libass.

Libass 0.17.4, FreeType 2.14.1, HarfBuzz 11.5.1 and FriBidi 1.0.16 are built
from pinned source archives and statically linked into the filter binaries.
Their source hashes and license notices are retained. Import-table inspection
confirmed that neither x86 nor x64 XySubFilter requires a separate libass,
FreeType, HarfBuzz or FriBidi DLL. See [integration details](libass-integration.md).

## Console workflow

The console app calls the shared production renderer without decoding video or
opening a player. Three modes are available:

| Mode | Measured work |
| --- | --- |
| `render` | Complete production RenderEx call |
| `parse` | ASS parsing/reconstruction and temporary subtitle cleanup |
| `composite` | Production composition after preparing actual draw inputs outside the timed interval |

Every invocation verifies its affinity mask and Above Normal process priority.
These runs used logical CPU 7, mask `0x80`, with one serial rendering thread.
Source/font loading, hashing and CSV writes are outside the selected timing
interval. Frame ranges are configurable. The comparison runner alternates
control/candidate order and records executable, subtitle and font hashes.

Nested diagnostic phase times overlap. Thread CPU time is useful in aggregate;
its per-frame resolution is coarse. Affinity and priority do not remove CPU
frequency changes or contention for shared caches and memory.

## Native optimization and measurements

The new lookup view borrows the frame's existing ordered child keys, avoiding a
temporary owned child-key sequence. Cache misses materialize immutable ownership
before retaining a key. Copy/assignment and the existing CreateHashKey API remain
owning. The control build explicitly creates an owned temporary snapshot.

All comparisons below use the same source, compiler, dependencies, 1080p output,
24000/1001 timestamps and 256 bitmap-cache entries. The full workload is 2,428
frames from 22:20 through EOF; 2,184 contain subtitle bitmaps. Lower times are
better. CPU values are medians of complete-pass thread CPU totals.

| Comparison | Owned control | Borrowed probe | Observed CPU reduction |
| --- | ---: | ---: | ---: |
| x86 complete render, ABBA | 32.234 s | 31.156 s | 3.34% |
| x64 complete render, initial ABBA | 34.297 s | 36.289 s | -5.81% |
| x64 complete render, repeated ABBA | 32.555 s | 31.734 s | 2.52% |
| x64 composition replay, ABBA | 17.078 s | 16.531 s | 3.20% |

Across all four complete x64 passes per variant, median CPU time was 33.320 s
owned versus 33.875 s borrowed (1.66% higher for the experiment). Median wall
time differed by only 0.36%, and the two batches disagreed in direction. This
does not establish a complete-render x64 speed-up on the busy PC.

**Build decision:** borrowed probes are enabled for x86. Normal x64 builds retain
owned snapshots. `/p:XyBenchmarkOwnedGroupKeys=false` enables the x64 experiment;
`true` selects the owned comparison path. The x64 composition improvement is
recorded separately and is not presented as a complete-render improvement.

The measurements concern native XySubFilter code. Libass speed was not an
acceptance criterion and was not compared against Native.

Raw comparisons and matching binaries/symbols/source snapshots are under
`benchmark-results/libass-native-20261026/`. The portable aggregate is
[20261026-libass-native.json](benchmarks/20261026-libass-native.json).

## Correctness and build checks

Both architectures passed 19 renderer/ownership contracts, two actual filter
option/Main-page contracts, 54 fixture cases, eight extended ASS scripts, all
2,428 sample frames at 256 and zero bitmap-cache entries, and all 2,428 frames
through composition replay. Native output had zero mismatches against the
preserved pixel control. Bitmap positions, sizes, counts and visible bytes are
included in the comparison; cache IDs and pitch padding are excluded.

Libass checks cover animation, seeking, retained frame ownership, backend
switching, embedded event flush/re-send, output formats and style edits. A
codec-private header without events loads successfully. Both architectures also
rendered real sample frames 32800..32802 near 22:48 twice, with zero repeat pixel
mismatches and nonempty output. Libass and Native have separate baselines; no
cross-engine pixel-identity or visual-superiority claim is made.

The pixel comparison tool now supports focused frame ranges and rejects missing
hashes. Three focused Python tests cover missing/reordered frames, timing-only
output and the historical extra empty EOF frame.

VS 18.11 produced LNK4020/LNK1103/LNK1318 debug-data errors during incremental
development builds. Release subtitles objects now embed type information with
`/Z7`; clean rebuilds of each filter and the console app avoid stale linker
data. When switching the group-key build property, clean-rebuild the affected
filter/console projects. Final binaries retain linker-generated PDBs. Existing
compiler warnings in legacy SIMD code remain; they predate this change.

Playback delivery, renderer compatibility and dropped frames still require a
separate Debug Zoom Player integration test when playback is authorized.

## Next focused work

The next allocation target is the per-event final `CompositeDrawItemList`:
short events still allocate linked-list node blocks. An inline-first sequence
could preserve ordered POSITION iteration while avoiding those blocks. Keep
the existing append-before-pointer-capture boundary and validate retained frames,
inverse clips, layer order, large events and both bitnesses before enabling it.

Other pending approaches include event/layout dependency reuse, mask/blend
fusion and byte-based cache budgets. The review's full approach list has not
been exhausted. Inspect the highest-cost frame ranges using the new phase modes;
do not infer zero playback drops from native timing alone.

The inline per-event draw-storage follow-up is now implemented and validated
on both architectures. See the [new native report](performance-20261026-inline-composite.md)
for measured x64/Win32 improvements, full pixel gates and remaining burst-frame
parsing costs.
