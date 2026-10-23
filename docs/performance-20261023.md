# Animated ending: first optimization

## Result

The first change fixes collisions in the shared subtitle-style table. In the same Debug Zoom Player playback benchmark, the optimized DLL reduced renderer-reported drops by **7.1%** against an unmodified build from this checkout. **Playback still drops frames.**

| Capture | New renderer drops | Process CPU seconds | Mean CPU, whole machine |
| --- | ---: | ---: | ---: |
| Previously installed 3.2.0.810 baseline | 1,088 | 77.95 | 9.55% |
| Unmodified source build, unprofiled | 1,094 | 78.25 | 9.59% |
| Style-hash optimization, unprofiled | 1,016 | 77.94 | 9.55% |

The source-control and candidate captures each span 102.002 wall seconds. End-to-end CPU consumption is essentially unchanged; the measurable playback benefit is fewer reported drops. This is one unprofiled control/candidate pair. The installed and profiled control runs produced 1,088–1,090 drops, giving some context for ordinary run variation.

Raw comparison: [comparison.json](../benchmark-results/comparison.json). Captures: [control](../benchmark-results/control-unprofiled/metadata.json), [candidate](../benchmark-results/candidate-unprofiled/metadata.json), and the [earlier installed baseline](../benchmark-results/baseline-20261023-05/report.md). Generated data and binaries are ignored by Git.

## Workload and controls

- Player: `Z:\zpAI\zplayer.exe`, verified Debug (`LOCAL`) MAX build, SHA256 `a5d987e8aaf18f0d3b3b3f732b74175247d8080314e0800da4e14f274166113b`.
- Media: `Subtitles_CPU_killer_on_end_credits.mkv`; default subtitle index 0, `Signs & Songs [eng] (ass) [default]`, explicitly enabled before each capture.
- Interval: 22:20 to EOF, 24:01.272. Rendering budget at 24000/1001 fps: 41.708 ms.
- Graph: LAV Splitter Source, LAV Video Decoder, XySubFilter, madVR. The decoder and renderer hashes match between captures.
- Machine: i7-2600K, four physical/eight logical CPUs, GTX 1060 6GB. Playback remained windowed with the existing settings.
- CPU comes from independent `GetProcessTimes` sampling. It includes the whole player, decoder, subtitle filter, renderer, and Debug logging. Renderer counters come from TCP request 1150.
- madVR's drawn-frame and average-FPS counters reflect renderer presentation behavior. They are not used as a source-frame denominator or a drop percentage.
- The subtitle DLLs are optimized Win32 Release builds with PDBs, built using the installed VS 2026 `v145` toolchain and the user-supplied Yasm. The player and helpers stay Debug.

The unmodified source DLL is `benchmark-results/source-control/XySubFilter.dll`, SHA256 `a10157831544c748a0c140bfd8a35c00e49ba42141b9be6d123b28164d57ea47`. The candidate is `benchmark-results/hash-candidate/XySubFilter.dll`, SHA256 `b0d26b4e056ffb0d8d0507d62b9fb6222f682f3214e0c678325b2d1a7f7a4123`.

## CPU findings

Visual Studio's installed CPU sampler collected native call stacks, resolved against each build's matching PDB. The comparison below uses the same 22:40–24:00 window in both runs: 7,907 control samples and 7,712 candidate samples, with zero lost events.

| Operation | Before | After | Interpretation |
| --- | ---: | ---: | --- |
| `STSStyle::operator==` | 7.11% exclusive | 0.21% exclusive | Repeated full style comparisons nearly disappear |
| Boost style-table insertion/collision traversal | 4.39% exclusive | 0.18% exclusive | Collision-chain work nearly disappears |
| `CRenderedTextSubtitle::ParseSSATag` | 22.25% inclusive | 26.37% inclusive | Tag parsing/evaluation, conversion and associated work remain substantial |
| `_malloc_base` called from XySubFilter | 14.08% inclusive | 15.25% inclusive | Allocation remains substantial |
| `_calloc_base` called from XySubFilter | 3.78% inclusive | 4.02% inclusive | Further allocation/initialization work |
| `CompositeDrawItem::Draw` | 9.31% inclusive | 11.32% inclusive | Bitmap composition and its callees |
| `MergeRects` | 0.23% inclusive | 0.35% inclusive | Small in both traces |

These percentages describe shares of player CPU samples and include changes in call frequency. Inclusive values overlap and must not be added. Optimized x86 stacks can omit inlined callers, so these are sampled observations rather than a complete phase-timing breakdown. The sampler started about ten seconds after the control playback capture began; both samplers cover the common comparison window.

Combined exclusive style-comparison/collision work fell from **11.50% to 0.39%**. The profiled candidate playback produced 1,024 new drops, consistent with the unprofiled candidate's 1,016; the profiled control produced 1,088. Raw evidence: [profile-comparison.json](../benchmark-results/profile-comparison.json), [control functions](../benchmark-results/baseline-source-control-02/cpu-functions.csv), [candidate functions](../benchmark-results/profile-hash-candidate/cpu-functions.csv).

ETW records both the installed DLL and the workspace DLL loaded, because the installed autoload helper remains available. All sampled XySubFilter instructions in this comparison window came from the intended workspace DLL in each run, as confirmed by [module-path samples](../benchmark-results/profile-hash-candidate/cpu-module-paths.csv).

The source confirms the style collision mechanism. `FwSTSStyle` interns styles through a Boost hash table. Its equality operator compares shadow, border, alpha, blur and other fields that its old hash omitted. A focused check generated 1,000 distinct values for each of shadow, border, alpha and blur: each family produced only **one** hash before the change and **1,000** afterward.

The offline candidate rendering made 3,772,178 parsed-tag cache queries with 441,923 hits: **11.72%**. This measurement comes from the native validation harness using its default 2,048-entry cache, rather than a query to Zoom Player. It supports investigating repeated syntax parsing and cache eviction next.

## Changes and validation

- [STS.h](../src/subtitles/STS.h): hash every field compared by `STSStyle::operator==`, retaining its equality behavior.
- [flyweight_base_types.h](../src/subtitles/flyweight_base_types.h): normalize positive and negative zero in the shared double hash so equal style values retain equal hashes.
- [Native validation target](../test/benchmark/render_subtitles.cpp): test hash distribution and signed-zero equivalence; render the default ASS track with its attached fonts loaded privately.
- [Benchmark tools](../test/benchmark/README.md): preserve playback captures, analyze native CPU samples, and compare both playback and rendered output.

All **2,428** source-frame midpoints in the interval were checked at 1920x1080. Every bitmap's position, size and visible RGBA bytes matched the control SHA256; **zero mismatches**, including 2,184 frames containing subtitles. Cache identity values and pitch padding were excluded. An older control CSV contains one additional empty sample after EOF, which the comparison excludes.

The offline render check averaged 70.17 ms before and 62.16 ms afterward, an 11.4% decrease. Its p95 decreased from 180.02 to 157.30 ms; p99 from 253.75 to 237.75 ms. Offline wall times can include other activity and are secondary diagnostic evidence. Both remain far beyond the frame budget in difficult sections. See [pixel-comparison.json](../benchmark-results/pixel-comparison.json).

Both runtime paths use the changed style and hash code: `CDirectVobSubFilter` through the shared renderer, and `XySubFilter` through its render-provider adapter. The playback test exercises XySubFilter with madVR; the direct renderer check covers shared pixel production. No claim is made about testing every other subtitle format or consumer.

## Next optimization

These follow-up changes have now been implemented and tested; see the [ASS parsing and allocation report](performance-20261023-ass.md) for the later builds, playback measurements and pixel checks.

Retain immutable parsed ASS syntax for the lifetime of active animated events, with an explicit memory bound and invalidation when subtitle text changes. This should avoid reparsing the same tags and repeatedly allocating their strings/lists when the small global MRU cache churns. Keep animation evaluation at each frame timestamp, preserve malformed-tag behavior, and measure allocation counts/cache misses alongside the same playback and pixel checks. Increasing a global cache limit alone needs a memory-cost measurement.

Follow with numeric-parameter conversion caching if the next profile still shows it as material. The current evidence does not justify starting with rectangle merging or blanket blur/SIMD rewrites.

## State after testing

The temporary per-user COM registration was removed, and the installed XySubFilter DLL's original hash was verified. Zoom Player's original completion action was restored to 2, and the benchmark player was closed. The optimized DLL/PDB and all measurement artifacts remain in `benchmark-results` for inspection. No software was installed by the agent; Yasm was supplied by the user.
