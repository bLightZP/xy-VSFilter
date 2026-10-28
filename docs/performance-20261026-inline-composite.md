# Native inline draw storage, 2026-10-26

This follow-up targets native x64 rendering. Playback was not run. Console
rendering uses one thread on logical CPU 7 (affinity 0x80), verified Above Normal
process priority. Builds are serial and restricted to the same logical CPU at
Normal priority; builds and measured rendering do not overlap.

## Change

The final per-event `CompositeDrawItemList` now stores its first word's three
layer owners directly. Additional words use contiguous spill storage. This
removes the inner ATL linked-list node-block allocation for single-word events.
The outer event list, bitmap partitioning, draw order and cache policies are
unchanged. Normal x64 builds continue using owned group-key snapshots; x86
continues using borrowed probes. The optional libass backend is unchanged.

Opaque POSITION values are one-based indices, stable during append. References
may move during spill growth. Rendering appends each line/clip region before
painting it; grouping captures pointers only after all `DoRender` appends finish.
The lists retain their layer owners throughout synchronous composition.

The expanded inline-list contract checks empty/inline/spilled draw storage,
257 ordered positions across growth, fresh empty layer slots, shared layer
ownership and destruction. Existing renderer fixtures cover inverse clips,
overlapping layers, multiword events and retained frames. A new
`draw-slot-spill.ass` case combines 257 alternating word runs, multiple lines,
four inverse-clip regions and an overlapping translucent polygon. Its control
is generated with the frozen old executable; diagnostics require more than
1,000 input draw slots to prove the spill path was exercised. The preserved x64
control produced 1,029 input slots at its peak and 72 nonempty frames.

## Comparison and validation

Raw records are preserved in `benchmark-results/inline-composite-20261026/`.
Control binaries, symbols and source snapshots were frozen from the previously
validated x64 owned-key and x86 borrowed-key builds before changing the code.
Timing comparisons use the same compiler (VS 18.11, v145), source ASS, private
fonts, CPU and 256 bitmap-cache entries. The full 2,428-frame sample window
is 22:20 through EOF (source frames 32128..34555), rendered at 1920x1080 and
24000/1001 fps from the previously extracted default subtitle track. Each ABBA batch
runs control/candidate/candidate/control in fresh serial processes. Thread CPU
and wall time are reported separately; nested phase totals overlap.

The two x64 ABBA batches both completed with exact pixel matches:

| Batch | Control CPU total, median | Inline CPU total, median | Observed reduction |
| --- | ---: | ---: | ---: |
| First | 35.133 s | 33.531 s | 4.56% |
| Repeated | 40.977 s | 31.625 s | 22.82% |
| Combined, four passes per variant | 35.359 s | 32.508 s | 8.06% |

The repeated batch includes an unusually slow 47.125 s control pass; its other
control was 34.828 s. Raw CPU totals, in execution order, were:

- First: control 34.375, inline 33.203, inline 33.859, control 35.891 s.
- Repeated: control 47.125, inline 31.438, inline 31.812, control 34.828 s.

Combined median full-render wall totals were 35.505 s and 32.353 s (8.88% lower).
Draw construction fell from 7.670 s to 6.692 s (12.75%); tree construction fell
from 1.790 s to 1.411 s (21.16%). These nested wall phases are not additive.
Concurrent PC use affects frequency, cache and memory traffic even with fixed
affinity and priority. The direction agreed in both batches; the exact size of
the improvement needs quieter-machine confirmation. The 22.82% second-batch
result should not be treated as an expected speed-up.

The median of per-pass wall-time p95 values fell from 33.951 to 30.303 ms;
p99 fell from 74.045 to 66.898 ms. The median per-pass maximum fell from
171.897 to 146.837 ms. Some native frames still exceed the 41.708 ms source
frame budget. This change does not establish zero dropped playback frames.

## Win32 regression comparison

The Win32 ABBA comparison also passed every pixel check. Median renderer-thread
CPU fell from 38.141 s to 34.664 s (9.12% lower); complete-render wall time fell
from 38.500 s to 34.797 s (9.62% lower). Draw construction fell 12.57% and tree
construction fell 19.62%. Raw CPU totals in execution order were control 40.141,
inline 33.859, inline 35.469, control 36.141 s. These are observations on the
same busy PC, subject to the x64 timing limitations above.

The frozen Win32 source snapshot predates an x64-only default project condition;
its Win32 group-key path and compiler/debug settings match the current build.

## Remaining frame spikes

The per-frame median across the four x64 candidate passes still exceeds the
source-frame budget in 49 frames. Correlating those samples with ASS event
start times identifies a separate burst of first-use work:

| Source frame | Position | Events starting since preceding frame | Parse/reconstruction wall | Complete render wall |
| --- | --- | ---: | ---: | ---: |
| 33448 | 23:15.081 | 3,996 | 89.232 ms | 146.837 ms |
| 33157 | 23:02.944 | 3,587 | 83.415 ms | 133.456 ms |
| 32780 | 22:47.220 | 3,200 | 74.085 ms | 128.135 ms |

These are measured phase medians and event-count correlations, not sampled
call-stack attribution. The next stutter-focused step is to split first-use
parsing, layout, cache admission and expiry costs around these frames. Evaluate
bounded immutable event preparation/reuse with a measured memory budget; if work
moves to subtitle loading or earlier frames, report that startup/time cost too.
Do not special-case the sample or change animation timestamps.

For a smaller independent allocation change, `CompositeDrawItemExVec` still
allocates per-event grouping metadata even for one draw. Inline-first metadata
can be tested against these newly frozen stable binaries. Preserve the pointer
capture boundary, rectangle partitioning, cache policy and full pixel gates.

## Completed correctness and builds

Both x64 and Win32 passed:

- 19 renderer/ownership contracts, including expanded draw-slot coverage.
- Actual VSFilter and XySubFilter option/Main-page contracts in a hidden host.
- 54 small fixture cases against their saved controls.
- Nine extended scripts, 2,428 frames each: reconstruction, zero alpha, polygon
  font fields, geometry reuse, multiline inverse clips, omitted numbers,
  edge blur/karaoke, grouping stress and draw-slot spill.
- All 2,428 real-sample frames with 256 and zero bitmap-cache entries, compared
  with the original pixel control.
- All 2,428 frames through production composition replay.

Every comparison has zero mismatches. The real-sample x86 and x64 outputs also
match each other directly. Bitmap counts, positions, dimensions and visible
bytes are hashed; cache IDs and pitch padding are excluded. x64 also passed a
complete diagnostic-enabled sample comparison. The spill control reaches
1,029 input slots, proving that a large single event spills across growth.

VSFilter, XySubFilter, the consumer and the console application were built for
both architectures with matching PDBs. PE machine fields were verified as AMD64
and I386 respectively. Filter version remains 3.2.1.900. Current binaries,
symbols and relevant source hashes match the frozen `stable-<architecture>`
manifests. No linker debug-data failures were present in the ten build logs.

Build outputs are under `bin/lib_18.0/x64/Release` and
`bin/lib_18.0/Win32/Release`. Portable aggregate measurements, artifact hashes
and source provenance are in
[20261026-inline-composite.json](benchmarks/20261026-inline-composite.json).
Playback, player settings and filter registration were not exercised.
