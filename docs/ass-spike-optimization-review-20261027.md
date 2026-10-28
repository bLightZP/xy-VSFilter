# ASS spike optimization review — 2026-10-27

The strongest next candidates are **retaining currently rejected constant-geometry event states, retaining prepared static vector paths, and avoiding repeated draw construction for unchanged event states**. These address specific remaining work in the optimized renderer. Larger generic caches or a longer lookahead horizon are weaker next steps.

The credit bursts and the earlier vector-art spike are different workloads. They should have separate acceptance measurements. In particular, reducing parse time alone will not bring the worst credit bursts within a 41.708 ms source-frame period.

## Evidence and scope

Reviewed the current native implementation against [the accepted spike-path report](F:/Git/xySubFilter/docs/performance-20261027-spike-paths.md), its [measurement manifest](F:/Git/xySubFilter/docs/benchmarks/20261027-spike-paths.json), and [the architecture map](F:/Git/xySubFilter/docs/knowledge-graph.md). All **47 recorded source-file hashes match the working tree**, for both architectures. This review includes the local optimizations already present; it does not treat the earlier October 25 findings as unimplemented.

Inspected the existing [default.ass](F:/Git/xySubFilter/benchmark-results/media/default.ass), so another extraction was unnecessary. SHA-256: `1ece3acc51ef4b091a6af7ab9f87d989e314c15d95e25ed883283d47f9656d72`, matching the benchmark input. It contains 99,165 dialogue events; 51,459 overlap 22:20–24:01.272.

Reanalyzed the four accepted candidate timing passes per architecture from `abba-g-*` and `abba-g-*-repeat`. Values below are **per-frame medians across four passes**, not the median of each pass's maximum. Frame indices are zero based; timestamps are source-frame midpoints at 24000/1001 fps. These are saved offline timings, not new playback measurements or measured gains from the recommendations.

The reproducible [analysis script](F:/Git/xySubFilter/docs/benchmarks/20261027-spike-followup-analysis.py) produces [the detailed JSON census and timing tables](F:/Git/xySubFilter/docs/benchmarks/20261027-spike-followup-analysis.json). Its exact-pattern counts describe this workload; they are not a substitute ASS interpreter or a proposal to recognize this file in production.

## Where the remaining time goes

| Frame / position | New / active events | x64 total | Win32 total | x64 parse | x64 draw setup | x64 bitmap stage |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 32249 / 22:25.073 | 426 / 426 | 50.92 ms | **74.77 ms** | 34.95 ms | 11.78 ms | 1.32 ms |
| 32693 / 22:43.591 | 3,587 / 3,758 | 44.07 ms | 43.59 ms | 10.74 ms | 18.83 ms | 8.94 ms |
| 32780 / 22:47.220 | 3,200 / 6,787 | **61.34 ms** | 57.32 ms | 11.62 ms | 21.92 ms | 16.19 ms |
| 32854 / 22:50.306 | 2,492 / 5,692 | 50.77 ms | 50.08 ms | 9.16 ms | 18.23 ms | 14.61 ms |
| 33157 / 23:02.944 | 3,587 / 6,787 | 59.73 ms | 55.04 ms | 12.96 ms | 20.10 ms | 12.15 ms |
| 33448 / 23:15.081 | 3,996 / 6,488 | **65.09 ms** | 62.84 ms | 15.67 ms | 22.73 ms | 15.81 ms |
| 33973 / 23:36.978 | 2,171 / 5,006 | 47.10 ms | 47.44 ms | 10.75 ms | 13.04 ms | 15.34 ms |

Parse includes active-event maintenance, reconstruction/replay and effect evaluation. Draw setup includes `DoRender`, dirty-bound acquisition and uncached-subtitle cleanup. The bitmap stage includes group-key lookup, allocation on misses and child drawing; it is **not a pure pixel-kernel measurement**. Composite includes tree/merge/group/bitmap stages. Full render also includes preparation and other cleanup. Nested phases and independently calculated medians must not be added as independent costs. See [the timing scopes](F:/Git/xySubFilter/src/subtitles/RTS.cpp:4445) and [composition scopes](F:/Git/xySubFilter/src/subtitles/draw_item.cpp:247).

At frame 33448, subtracting the entire 15.67 ms parse-stage median from 65.09 ms still leaves about **49.42 ms**. That is an illustrative ceiling on parse-only improvements, not a predicted implementation speedup. Frames immediately after activation also exceed budget: frame 32782 takes 47.44 ms, including 10.32 ms draw setup and 16.22 ms bitmap work. This is not solely a first-frame parsing problem.

## Ranked candidates

| Rank | Candidate | Main benefit | Confidence / implementation risk |
| --- | --- | --- | --- |
| 1 | Admit constant drawing-mode programs to guarded preparation/reuse | Large credit activations and subsequent step changes | High confidence in eligibility gap; moderate semantic/cache-budget risk |
| 2 | Retain prepared static vector geometry; streamline cold polygon parsing | The 22:25 Win32 maximum and cold vector-art starts | High confidence in discarded work; moderate retention/parser risk |
| 3 | Reuse immutable draw descriptors and overlay results for unchanged states | Credit activation overhead and several following frames | High confidence in repeated work; moderate ownership/invalidation risk |
| 4 | Reuse verified grouping topology and unchanged group keys | Repeated composition across large active sets | Promising, but exact topology hit rate is unmeasured |
| 5 | Optimize short-span composition and dispatch | Residual bitmap cost in dense rectangle effects | Plausible; needs finer counters before selecting a kernel |
| 6 | Give preparation a bounded opportunity in actual provider cadence | Translate offline lookahead wins into playback | Integration opportunity; depends on real packet lead and request cadence |

### 1. Constant `\p` inside a transform unnecessarily defeats prepared layout

**Workload evidence.** There are **39,510 events** in the measured window matching this structural pattern: a single 15×15 drawing, fixed position/clip/metrics, two step changes of shadow alpha, and `\p1` inside the outer transform. The outer step is at 1–120 ms after event start; the inner step is at 970–3930 ms. Each pair of step endpoints is equal. A shortened structural example is:

```text
{...\pos(x,y)\t(a,a,\4a&HFx&\p1\t(b,b,\4a&H00&)\clip(0,0,1920,81)}m 0 0 l 15 0 15 15 0 15
```

At frame 32780, 5,450 of the 6,787 active events have this exact program structure, including 2,560 of the 3,200 new events. At frame 33448, the corresponding counts are 5,200 active and 3,240 new. The renderer's existing logs record 39,646 prepared-layout fallbacks. The 39,510-event family is strongly consistent with most of that count, but the logs do not record rejection reasons, so this is not a measured attribution of every fallback.

**Code mechanism.** [HasLayoutAnimation](F:/Git/xySubFilter/src/subtitles/RTS.cpp:3214) rejects every command inside a transform except its explicit allowlist; `CMD_p` is rejected. However, [CMD_p evaluation](F:/Git/xySubFilter/src/subtitles/RTS.cpp:3052) assigns `m_nPolygon` without consulting `fAnimate` or time. [Transform evaluation](F:/Git/xySubFilter/src/subtitles/RTS.cpp:3093) always visits its embedded commands. In this evaluator, that drawing-mode command is constant even when nested inside a transform.

During preparation, [the ordinary stable-interval retention branch](F:/Git/xySubFilter/src/subtitles/RTS.cpp:3800) is disabled by `!m_preparingAhead`. The failed layout eligibility then prevents guarded prepared-state retention, and [PrepareUpcoming](F:/Git/xySubFilter/src/subtitles/RTS.cpp:4030) recycles the result. Syntax can survive, but the event is reconstructed at activation and again as its interval boundaries are crossed. Current interval caching already avoids reconstruction between those boundaries; that optimization should be preserved.

**Proposed change.** Classify dependencies by the existing evaluator's behavior. Start narrowly with constant drawing-mode/baseline commands proven time independent, rather than accepting arbitrary metric animation. Preserve the resulting polygon mode in the immutable layout plan and use the existing exact context checks. Alternatively, provide a guarded staged version of the small stable-interval snapshot, so successful initial states need not acquire a full general layout plan just to survive preparation.

Do not simply remove `!m_preparingAhead`: the current code intentionally retains only states with complete source/style/context guards. Keep retained bytes bounded. General prepared states have a minimum 8 KiB reconstruction charge, versus 4 KiB for eligible small polygons; moving thousands of entries to that path can exhaust the 64 MiB budget sooner. A compact guarded snapshot may be preferable to increasing the budget again.

**Compatibility and validation.** Use the current parsed tag tree. The source has incompletely closed nested transform syntax, and [the parser terminates a parameter span at the first closing parenthesis](F:/Git/xySubFilter/src/subtitles/RTS.cpp:2450). A new balanced parser would change the input's interpretation. Preserve command order, zero-endpoint behavior, integer alpha conversion, transform nesting and style resets. Measure rejection reasons and retained bytes, then compare the actual bursts plus times just before/at/after both steps. Include seeks, source/style changes and zero-lead packet delivery.

**Expected scope.** This should remove an identifiable preparation/reconstruction gap with early packets. It may also reduce later reconstruction in zero-lead runs after first activation. It does not eliminate the first cold parse when the event has only just arrived, nor the remaining draw/composition cost.

### 2. Static vector preparation is discarded before the worst Win32 frame

**Workload evidence.** Frame 32249 starts **426 static, unique drawing events**, all using `AngreSigns`, totaling **823,363 source characters**. They contain no `\t`, and all end at 22:30.080. Four exceed the 8,192-character preparation cutoff; the other 422 are eligible to be attempted. The four long source lines are 88221, 88237, 88242 and 88260. The Win32 parse-stage median is **57.15 ms** of a 74.77 ms frame; x64 parse is 34.95 ms of 50.92 ms.

**Code mechanism.** [CreateSubtitle](F:/Git/xySubFilter/src/subtitles/RTS.cpp:3825) normally caches static subtitles, but only animated prepared-layout states receive `m_preparedContext`. [PrepareUpcoming](F:/Git/xySubFilter/src/subtitles/RTS.cpp:4030) then removes/recycles states without that context, including static drawings. [Recycle](F:/Git/xySubFilter/src/subtitles/RTS.cpp:1810) destroys their owned words and paths.

[CPolygon::ParseStr](F:/Git/xySubFilter/src/subtitles/RTS.cpp:811) can reuse a path-cache hit, but the usual path-cache population occurs in [PaintFromRawData](F:/Git/xySubFilter/src/subtitles/RTS.cpp:354). Preparation does not paint, and static events do not enter the animated layout-plan branch that freezes paths. Thus a newly parsed unique static path has no guaranteed retained geometry after preparation. The global tag cache is not equivalent to retaining those vertices.

**Proposed change, first stage.** Retain a bounded immutable static path/metric product, or a staged static subtitle with complete context validation. Publish parsed geometry before discarding temporary words. Preserve exact script scale, polygon mode, baseline and coordinate rounding, and account for path buffers and source ownership. The existing [FreezePreparedPath](F:/Git/xySubFilter/src/subtitles/RTS.cpp:762) and exact polygon-scale lookup provide useful pieces.

Do not return a new staged static state through [GetSubtitle's unconditional static-cache fast path](F:/Git/xySubFilter/src/subtitles/RTS.cpp:3538) before validating its preparation context. Keep collision placement at activation where required. Verify retention does not merely evict earlier active states and move the spike elsewhere.

**Proposed change, cold path.** [ParseStr's miss path](F:/Git/xySubFilter/src/subtitles/RTS.cpp:838) copies the polygon string, performs seven whole-string replacements, creates command substrings and converts coordinates with `wcstod`. A source-span command scanner with sensible point-buffer reservation is a concrete next experiment for this 823k-character burst. Preserve legacy accepted numeric syntax and rounding; changing numeric conversion is a separate, higher-risk step. Measure scan/conversion/allocation separately before claiming a specific subroutine dominates the 57.15 ms parse phase.

Retaining preparation mainly helps when the source is available early. Improving the cold parser also helps zero-lead delivery and seeks. Do not simply raise the 8,192-character cutoff: the preparation budget is checked between events, and making a long event eligible can turn an earlier frame into another spike. Longer paths need bounded/incremental work or the faster cold path.

### 3. Stable events still rebuild draw objects and resolve cached overlays every frame

**Workload evidence.** Frame 32780 contains 5,995 instances of the same square literal, with 33 unique visible literal spans overall. Frame 33448 contains 5,720 square instances and 32 unique literal spans. Draw setup still costs 21.92 and 22.73 ms respectively. The accepted candidate logs contain roughly **4.81 million overlay queries with only 4,425 misses** over the full pass. This is strong evidence for optimizing the successful reuse path, not enlarging the overlay cache.

**Code mechanism.** [CLine::PaintAll](F:/Git/xySubFilter/src/subtitles/RTS.cpp:1267) recreates paint machines and draw items on every render, even when `GetSubtitle` returned an unchanged interval-cached event. The accepted code already combines a word machine and its three wrappers into one allocation and retains three geometry/draw keys per word. Nevertheless, [CreatePaintMachines](F:/Git/xySubFilter/src/subtitles/xy_overlay_paint_machine.cpp:154) allocates a new machine, and [CreateDrawItem](F:/Git/xySubFilter/src/subtitles/draw_item.cpp:106) separately allocates each present layer.

[GetDirtyRect](F:/Git/xySubFilter/src/subtitles/draw_item.cpp:24) requests overlay bounds. [CalcDirtyRect](F:/Git/xySubFilter/src/subtitles/xy_overlay_paint_machine.cpp:24) resolves/paints the overlay when its new wrapper has no result. [PaintBody](F:/Git/xySubFilter/src/subtitles/xy_overlay_paint_machine.cpp:76) constructs and hashes lookup keys even though geometry keys have already been computed for the machine. Bounds are requested in `PaintAll` and again while building the composition tree. The second request benefits from the frame-local overlay, but still recomputes intersections/rounding.

**Proposed change.** Retain immutable per-event/per-word draw descriptors for exact unchanged states, with frame-specific group membership kept separately. Cache final dirty bounds using full position, clip, transform, scale and output-context identity. Reuse the already computed geometry key for compatible overlay lookups; preserve the distinction between true subpixel keys and quantized raster keys. Consider a weak last-overlay owner, or a small explicitly byte-accounted owner, to avoid repeated global cache probes.

Start with stable single-word events and keep a fallback for movement, changing appearance, vector clips and karaoke. Do not mutate draw objects that an older frame can still reference. `use_addition_draw` is currently mutable composition state; separate it from retained immutable descriptors or recompute it safely. Avoid making each active word a permanent strong owner of a large raster image outside the existing memory budget.

**Validation.** Count ordinary C++ allocations and key constructions, not only `xy_alloc_calls`: the existing tracked allocator counters do not cover every `boost::make_shared` or ATL allocation. Measure exact descriptor/bounds hits, overlay probes, ownership bytes and draw-setup tails. Compare consecutive burst frames, not just the first one. This candidate targets work performed on the current frame and therefore matters even when packets arrive with no lead.

### 4. Reuse exact grouping topology before attempting another band-container rewrite

**Workload evidence.** Frame 32780 expands 6,787 input items into **50,017 member references and 46,412 grouped draw references**, across 51 groups. Frame 33448 has 47,880 member references and 51,368 draws across 107 groups. At frame 33157, tree/merge/group medians are 2.28/6.60/1.70 ms. The 1,537 adjacent nonempty frames with equal grouping counters in the first x64 pass make topology reuse worth investigating, but equal counters do **not** prove equal rectangles or membership.

**Code mechanism.** [CompositeDrawItem::Draw](F:/Git/xySubFilter/src/subtitles/draw_item.cpp:247) reconstructs the entire tree, calls [MergeRects](F:/Git/xySubFilter/src/subtitles/draw_item.cpp:406), expands membership and reassembles layer-ordered lists every frame. Existing bitmap-cache lookup happens [after that work](F:/Git/xySubFilter/src/subtitles/draw_item.cpp:619). [Group-key hashing](F:/Git/xySubFilter/src/subtitles/cache_manager.cpp:440) walks all children again, even when most children stayed unchanged.

**Proposed change.** Retain a bounded previous topology: ordered input identity, exact dirty rectangles, event/word boundaries, layer presence and draw-method decisions. A verified match can reuse group rectangles and member indices while appearance changes produce updated draw keys. Store indices/owned metadata, not pointers into frame-local arrays. For unchanged ordered group children, retain the group key; for changed children, rebuild only affected groups. Hashes may accelerate comparisons but must not replace equality/invalidation checks.

Keep the exact current bitmap partition, ordering and rounding. The existing rejected contiguous/inline-band experiment already regressed composition despite matching pixels; repeating that storage substitution is not supported by the evidence. Topology reuse removes repeated work rather than changing the same sweep's containers.

**Validation.** Add an exact topology comparison counter first, and report hit rate on the over-budget frames. If geometry changes defeat reuse on those frames, lower the priority. Check equal-layer/read-order events, offscreen/empty bounds, inverse clip, the 128-item draw-method threshold, and consumers that combine excessive bitmap counts.

### 5. Measure and optimize short-span composition, preserving tiny alpha contributions

**Workload evidence.** The sample's dense polygons are 15×15 source squares with `\blur0\bord0`, and most use a rectangular clip to the top 81 pixels. Frame 32780 has 6,655 clipped events and no inverse clips; frame 33448 has 6,360 and no inverse clips. The remaining bitmap-stage medians are about 12–16 ms in the major bursts. Text effects also split output into narrow horizontal regions.

**Code mechanism.** The accepted [fused packed single-color path](F:/Git/xySubFilter/src/subtitles/Rasterizer.cpp:2004) processes 16 coverage samples at once, then uses a per-pixel tail. Raster padding, fractional position and group clipping determine the actual span widths; the source's 15-pixel width alone does not establish the scalar fraction. [GroupedDrawItems::Draw](F:/Git/xySubFilter/src/subtitles/draw_item.cpp:619) also dispatches every child in a missed group, and [AlphaBltDraw](F:/Git/xySubFilter/src/subtitles/draw_item.cpp:62) resolves clip/overlay owners before the fused path rejects an empty area or zero opacity.

**Proposed change.** First record post-intersection width/height histograms, zero/empty draws, cache-key time and pixels handled by the vector/tail paths. If short spans dominate, add exact 4/8-pixel packed blocks and reduce setup for tiny unclipped draws. If dispatch dominates, reuse prevalidated layer descriptors and precomputed intersections instead. Move proven no-op checks earlier only while retaining the original bounds/grouping contract.

Do **not** discard the rectangles as visually invisible. Their `\1a&HFE&` means opacity 1, not 0; repeated contributions can accumulate. The current fixed-point coverage formula can produce a nonzero pixel for that opacity. Preserve the source alpha, blending order and rounding. Do not coalesce overlapping squares by summing opacity or replacing them with a single rectangle unless exact equivalence is established.

Planar/additive fusion and inverse-mask redesign are lower-priority for these particular bursts: the saved benchmark uses packed output, and the large compositions exceed the current additive-selection cutoff. These remain separate workloads to measure, not explanations for the recorded packed credit spikes.

### 6. Preparation needs actual provider opportunities and packet lead

The accepted packet model already establishes the limitation. At frame 32780, total insertion-plus-render cost with preparation enabled is approximately **104.13 ms at zero lead**, 79.53 ms at 250 ms lead and 59.30 ms at 1,000 ms lead. The zero-lead run prepares no events, and insertion is only a few milliseconds. Those are model results, not a measurement of the media splitter during real playback.

[PrepareUpcoming](F:/Git/xySubFilter/src/subtitles/RTS.cpp:3975) runs synchronously after a native render, including direct blank render requests, with a 4 ms allowance and a 75%-of-source-period deadline. It cannot inspect packets not yet delivered. The offline console requests every frame, but [SimpleSubPicProvider](F:/Git/xySubFilter/src/subpic/SimpleSubPicProviderImpl.cpp:175) and [XySubRenderProviderWrapper2](F:/Git/xySubFilter/src/subpic/XySubRenderProviderWrapper.cpp:376) skip blank/nonactive requests and reuse static frames. Consequently, merely fixing preparation retention does not guarantee preparation will run before the next event in playback.

**Proposed change.** Measure packet arrival lead, actual renderer-call cadence and preparation time around the identified timestamps. If delivered events lack preparation opportunities, consider a small explicit preparation service point on existing provider requests, including cached/blank output, with rendering context initialized and the same lock/time budget. Keep ingestion work and any displaced frame cost inside the benchmark. This needs both native adapters; it should not be an unmeasured untimed prepass or an assumed worker-thread speedup.

The native shared changes above serve both `CDirectVobSubFilter` and `XySubFilter`/consumer paths. The optional libass backend bypasses these native internals; results and improvements must be labeled by backend. Actual playback validation must use the required Debug (`LOCAL`) Zoom Player and helpers, with the default test track, following [the benchmark instructions](F:/Git/xySubFilter/test/benchmark/README.md).

## Lower-priority directions and prerequisites

- **Whole-frame interval reuse:** only 171 adjacent nonempty frames have identical hashes in the first x64 candidate pass; those requests total 166.84 ms and none exceeds 4.19 ms. Reusing complete identical output is useful elsewhere, but these measurements make it a weak primary solution for the remaining spikes. Component/topology reuse can help while the full image changes.
- **Wrapper invalidation must precede longer frame lifetimes:** [XySubRenderProviderWrapper2](F:/Git/xySubFilter/src/subpic/XySubRenderProviderWrapper.cpp:338) still overwrites an earlier geometry/alpha invalidation with the bitmap-count comparison at line 358. This is a correctness prerequisite for extending output reuse, not a demonstrated cause of these timing spikes.
- **More sorting/expiry work:** the accepted logs already show 81 order builds versus 2,103 hits, and deadline-based expiry. Prioritize reconstruction and drawing unless new profiling contradicts that evidence.
- **Bigger generic caches:** overlay hit rate is already approximately 99.91%; the rejected-event retention gaps are eligibility/lifetime issues. Memory budgets and allocation changes were not independently isolated in the accepted combined experiment.
- **More blur/SIMD everywhere:** most rectangles in the burst have zero blur. Raster cache misses are comparatively rare; inspect actual short-span and dispatch costs before optimizing convolution or adding a new instruction-set requirement.
- **Changing ASS semantics or the media:** no proposed optimization depends on a filename, style name, timestamp or particular square path. The unusual source syntax and low opacity values are compatibility inputs, not artifacts to normalize away.

## Suggested implementation and validation sequence

1. Add low-cost diagnostic counters for layout rejection reasons, retained/discarded preparation products, ordinary draw allocations and exact descriptor/topology hits. Split the bitmap stage into key/cache/allocation/dispatch/pixel work. Keep diagnostic performance separate from final uninstrumented timing.
2. Implement the narrow constant-`\p` eligibility or guarded-interval retention change independently. Benchmark the 17 source-selected bursts and their following 120 ms. Track retained memory and fallback counts; do not accept a cache-budget increase as a substitute for proving reuse.
3. Implement static-path retention independently, then the polygon source scanner if the cold path still warrants it. Include frames 32249 and neighboring requests explicitly; this important 426-event spike is **outside the existing ≥1,000-new-event burst cohort**.
4. Prototype immutable draw/bounds reuse. Follow with topology reuse only if exact-match counters support it, and short-span kernels only if width/dispatch measurements support them.
5. Repeat serial x64 and Win32 ABBA runs over the full 2,428-frame interval and the packet model at 0/250/1,000 ms lead. Measure current-frame and total CPU costs, per-frame medians, p99/max and frames over 41.708 ms, plus preparation overrun and peak retained/committed memory. Include cold seeks and eviction pressure.
6. Require exact frame bytes and bitmap geometry/order against the accepted baseline, with the existing contracts and targeted tests for any changed eligibility/parser/blend behavior. Cover both native adapters and relevant output formats. Finally measure real playback with the required Debug player, actual packet lead and dropped-frame statistics.

This review adds documentation and a reproducible analysis of existing artifacts. It does not change renderer behavior, run new performance benchmarks, or claim a measured speedup for any proposed change.

## Implementation follow-up

The first three candidates are implemented in the shared native renderer.
[The implementation and fresh x64/Win32 comparisons](performance-20261027-top-three.md)
record exact-output gates, individual spike timestamps and neighboring requests,
all 17 fixed activation bursts, memory, packet-delivery models and remaining
limitations. The saved timings and analysis above remain the pre-change evidence.
No playback tests were performed for this follow-up.
