# ASS rendering optimization review

Date: 2026-10-25. Reviewed checkout: `codex/sol-6.1-xhigh-v3.2.1.900`, HEAD `e61a07fa41d17f23e2ee848354f5aedd9bb616fd`, including the existing uncommitted changes to `draw_item.cpp`, `draw_item.h`, and `flyweight_base_types.h`.

The largest remaining opportunity is to avoid reconstructing and assembling unchanged ASS state. The renderer already reuses most rasterized geometry in the saved stress workload; a cache hit still leaves substantial event evaluation, object construction, grouping, key lookup, and composition work. Optimizing those stages is a stronger starting point than replacing the rasterizer or simply enlarging every cache.

This is a source review supported by existing measurements and a read-only census of the saved ASS script. No renderer changes, builds, native tests, profiling sessions, playback, registry changes, or software installation were performed. Existing modified files were left untouched. Performance improvements proposed below are **not newly measured speed-ups**.

## Scope and existing evidence

The review followed the [knowledge graph](knowledge-graph.md), then inspected event ingestion and segmentation in `STS`/`SubtitleInputPin`, the shared `RTS` evaluator and layout code, overlay/clipper machines, cache keys and MRUs, rasterization/blur/composition, and both integration paths:

| Runtime path | Relevant call chain |
| --- | --- |
| VSFilter | [CDirectVobSubFilter::Transform](../src/filters/transform/vsfilter/DirectVobSubFilter.cpp#L354) → [SimpleSubPicProvider::LookupSubPicEx / RenderTo](../src/subpic/SimpleSubPicProviderImpl.cpp#L170) → shared `CRenderedTextSubtitle::RenderEx` → subpicture alpha blending |
| XySubFilter | [XySubFilter::RequestFrame](../src/filters/transform/vsfilter/xy_sub_filter.cpp#L1181) → [XySubRenderProviderWrapper2](../src/subpic/XySubRenderProviderWrapper.cpp#L419) → shared `RenderEx` → external consumer; the repository's [XySubFilterConsumer](../src/filters/transform/vsfilter/xy_sub_filter_consumer.cpp#L280) requests and blends the delivered bitmaps |

The solution, DLL project files, shared property files, benchmark target, and CI build definitions were also inspected. The shared subtitle library is linked into the DLL targets. A change in shared rendering can therefore affect both integration paths, while consumer negotiation and bitmap combining require additional XySubFilter-specific checks.

### Work already present

Do not count these as new proposals: complete style hashing, cached numeric tag conversion, retained active-event syntax/text spans, immutable polygon-path sharing, inline word/line/effect storage, a bounded empty-subtitle pool, stable reconstruction intervals, retained per-word keys, shared layer geometry, and frame-local alpha scratch storage. Their implementation and historical validation are described in the [ASS report](performance-20261023-ass.md), [reconstruction report](performance-20261023-reconstruction.md), and [x64/grouping follow-up](performance-20261025-x64-grouping.md).

The current grouping experiment already replaces the final group's linked list and borrows the group-key probe. It does **not** replace the many intermediate grouping lists. Its saved x64 playback pair is mixed: 40 versus 53 drops, but 48.78125 versus 46.640625 CPU seconds. That is not an established performance win, and should not be treated as one when choosing the next baseline.

### What the saved measurements actually support

The final saved x86 profile contains 3,636 samples, 3,632 stacks, and zero lost events. Selected inclusive shares are `CompositeDrawItem::Draw` 20.63%, `CLine::PaintAll` 11.55%, `GetSubtitle` 7.65%, `DrawItem::AlphaBltDraw` 5.69%, and `ParseSSATag` 4.10%. `GroupedDrawItems::CreateHashKey` accounts for 1.98% exclusive samples. These shares overlap, are percentages of the sampled playback process, and must not be added or interpreted as potential speed-ups. This profile predates the current grouping experiment and is not a profile of the current x64 helper. See [functions](../benchmark-results/word-keys-exact-bitmap256-profiled/cpu-functions.csv) and [trace quality](../benchmark-results/word-keys-exact-bitmap256-profiled/cpu-analysis.json).

The saved current grouped x64 native run at 256 bitmap entries reports:

| Operation | Queries / hits | Interpretation |
| --- | ---: | --- |
| Overlay cache | 4,810,875 / 4,806,450 | 99.91% hits, but about 1,981 lookups per rendered frame across 2,428 frames |
| Raw path cache | 399,601 / 398,530 | 99.73% hits; raw path creation is rarely needed in this workload |
| Bitmap cache | 228,488 / 102,469 | 44.85% hits; composition is still frequently necessary |
| Group-key interning | 354,507 / 102,469 | More probes than bitmap requests because misses perform a second interning lookup |
| Reconstruction reuse | 3,083,917 hits | Large existing benefit; 5,918 peak retained entries and 26,157,056 accounted bytes |

At 1,024 bitmap entries, bitmap hits rise only to 111,897/228,488, or 48.97%. Native peak pagefile commit rises from about 351.1 to 357.1 MiB. These runs establish cache behavior and saved correctness results, not a reliable speed comparison: regression runs overlapped. Sources: [256-entry log](../benchmark-results/resume-20261025-grouped-x64/render256.log), [1,024-entry log](../benchmark-results/resume-20261025-grouped-x64/render1024.log).

A read-only census of `benchmark-results/media/default.ass` found **51,459 nonzero-duration dialogue events overlapping 22:20–24:01.272**, peaking at **6,787 simultaneous events at 22:47.180**. Of those events, 50,991 contain a literal `\t(`, 51,033 contain `\blur`, 49,801 contain `\clip(`/`\iclip(`, and 43,929 contain a drawing-mode `\p` followed by a nonzero digit. These are textual occurrence counts, not evaluated visibility or a classification of valid ASS semantics. Peak overlap was computed from start/end times, processing ends before starts at equal timestamps. Script SHA256: `1ece3acc51ef4b091a6af7ab9f87d989e314c15d95e25ed883283d47f9656d72`.

Generated benchmark artifacts are local and ignored by Git; their links may not exist in another checkout. The tracked historical reports provide portable summaries.

## Priority overview

Priority here means investigation/implementation value, not bug severity. Confidence describes the source evidence; end-to-end gains remain unmeasured.

| ID | Opportunity | Expected scope of benefit | Effort / risk | Priority |
| --- | --- | --- | --- | --- |
| 1 | Separate immutable event/layout state from animated properties | Large for dense animated text and drawings | High / high compatibility sensitivity | High |
| 2 | Replace remaining frame object/list churn and repeated grouping work | Broad across dense ASS, including cache hits | Medium–high / ownership and ordering | High |
| 3 | Maintain active-event order and expiry incrementally | Dense overlapping-event workloads | Medium / streaming invalidation | High |
| 4 | Fuse alpha-mask generation with common blending paths | Large/overlapping bitmap-cache misses | Medium–high / exact arithmetic | High |
| 5 | Remove unused `\be` scratch allocation and reuse row buffers | Blur misses and high `\be` pass counts | Low–medium / localized | Early, workload dependent |
| 6 | Repair coupled cache sizing and reduce group-key miss overhead | Dense groups, cache tuning, miss-heavy animation | Low for sizing; medium for lookup redesign | High |
| 7 | Improve karaoke and fractional-transform hash discrimination | Karaoke and slowly changing geometry | Low–medium / equality contract | Medium |
| 8 | Expose safe output-validity intervals above reconstruction | Step animations and stable phases | Medium–high / temporal correctness | Medium–high |
| 9 | Reduce clip-related repeated work and oversized masks | Inverse/vector clips and heavily clipped effects | Medium / bounds and composition | Medium–high |
| 10 | Make memory policy reflect bytes and actual ownership | Large images, long sessions, resizing and seeks | Medium / cache lifetimes | Medium |

## Detailed findings

### 1. Retain event structure and update only the properties that change

**Observed.** [GetSubtitle](../src/subtitles/RTS.cpp#L3045) can retain a completed animated subtitle only while its reconstruction interval is constant. Admission at lines 3248–3260 further restricts it to small single-word text or polygons, excludes several clip/effect cases, and applies entry/byte limits. A continuously interpolating property sets `m_reconstructionStable = false` in [CalcAnimation](../src/subtitles/RTS.cpp#L3023). The next render then replays the event's cached tag lists, reconstructs words and styles, and calls `CreateClippers`/`MakeLines` again.

Retained tag syntax saves tokenization, not evaluation or object reconstruction. [ParseString](../src/subtitles/RTS.cpp#L2028) still performs escape replacement, whitespace scanning, substring creation, and word construction on these rebuilds. The evaluator still walks a linked `AssTagList` and executes the tag switch. Appearance-only transforms can consequently trigger layout reconstruction even though glyph metrics and paths are unchanged. Even a stable animated multiword event falls outside the current whole-subtitle retention rule.

**Recommendation.** Introduce a bounded immutable event representation with pre-tokenized text/drawing spans, ordered style operations, and dependency flags. Separate dependencies for layout, geometry, blur, clipping, placement, and color/opacity. Reuse word boundaries and layout for appearance-only changes; evaluate the current color/opacity into small frame values. Extend stable-state retention to bounded multiword events as a smaller intermediate step. Preserve the existing renderer as the fallback for complicated events.

Do not mutate shared flyweight styles or objects retained by earlier frames. Start with precisely classified color/alpha transforms, then broaden support after equivalence checks. `\r`, drawing-mode transitions, relative font sizes, multiple/nested transforms, HTML interleaving, karaoke state, and malformed parameters all make command order significant. The current parser stops parameter scanning at the first closing parenthesis ([RTS.cpp:2109](../src/subtitles/RTS.cpp#L2109)); substituting a stricter, differently nested parser would be a behavior change.

**Validation.** Count reconstructions by rejection reason and property dependency, and separately time tag evaluation, word creation, and layout. Compare continuously animated colors against geometry-changing controls, including edits, backward seeks, and retained-frame lifetimes. Expected impact is high, but the safe subset must be measured before committing to a full evaluator redesign.

### 2. Make frame assembly compact and avoid rediscovering unchanged groups

**Observed.** Every word painted by [CLine::PaintAll](../src/subtitles/RTS.cpp#L1235) constructs paint machinery and draw items. [CreatePaintMachines](../src/subtitles/xy_overlay_paint_machine.cpp#L154) allocates a shared word machine and one to three layer wrappers; [CreateDrawItem](../src/subtitles/draw_item.cpp#L103) allocates each layer's draw item. A simple body-only word still needs these three shared allocations, even when its keys and geometry are cached.

[CreateDrawItemExTree](../src/subtitles/draw_item.cpp#L287) adds per-item rectangle-ID lists, per-rectangle shared list objects, and list nodes. [MergeRects](../src/subtitles/draw_item.cpp#L318) creates horizontal bands from vertical breakpoints, visits every band intersected by each rectangle, and repeatedly appends item lists. Vertically merging bands appends their lists again; repeated references are removed only later when group IDs are assigned. The work and intermediate membership storage can grow quadratically for adversarial rectangle arrangements. A spatial sweep cannot eliminate an intrinsically quadratic output, but it can avoid much of the temporary work.

**Recommendation.** First replace these remaining frame-local linked structures with reusable contiguous arrays, index ranges, and generation-stamped membership deduplication. Separate frame-owned draw records from immutable cache-owned keys: an arena can own short-lived records, but retained bitmaps/keys must not reference it. Reuse stable group's membership and order when bounds are unchanged, even when its colors change. Prefer these bounded steps before changing the grouping algorithm itself.

Geometry is also obtained before the bitmap-cache lookup: [OverlayPaintMachine::CalcDirtyRect](../src/subtitles/xy_overlay_paint_machine.cpp#L24) paints/looks up the overlay to discover its bounds. Per-frame machines forget that result at the end of the frame. Reusing immutable bounds or a validated geometry handle would reduce millions of successful lookups; retaining strong overlay references requires byte accounting as described in finding 10.

**Validation.** Measure allocations, rectangle-band memberships, duplicate memberships, group size, and grouping time versus active-word count. Preserve subtitle order and each subtitle's shadow → outline → body order. `DecideDrawMethod` changes behavior above 128 composite items ([draw_item.cpp:470](../src/subtitles/draw_item.cpp#L470)); dropping or regrouping items must not silently change this decision or antialiased overlap output. The existing inline final-group change alone does not address these remaining structures.

### 3. Stop sorting and sweeping unchanged event sets every frame

**Observed.** [ParseScript](../src/subtitles/RTS.cpp#L3393) scans the retained subtitle list for expiration, copies all active events into a temporary array, and `qsort`s them by layer/read order on every render. [ExpireParsedTags](../src/subtitles/RTS.cpp#L2884) independently scans every retained parsed-event entry. Animation causes repeated renders within the same segment, when the active set and ordering have not changed. With thousands of simultaneous events, this repeats substantial pointer chasing and sorting.

[CScreenLayoutAllocator::AdvanceToSegment](../src/subtitles/RTS.cpp#L1637) also searches the active event array for each retained collision rectangle, even when called repeatedly with an unchanged segment. Its importance depends on how many events use automatic collision placement; hard-positioned ASS effects often bypass that path.

**Recommendation.** Cache the render-order vector per active segment/generation, maintain an expiry queue or sorted end-time structure, and skip collision-set maintenance when membership is unchanged. Maintain separate generations for source/style/layout changes. Do not use the segment array index alone as a validity key: embedded subtitle [Add](../src/subtitles/STS.cpp#L2532) splits and inserts segments as samples arrive.

Preserve the exact layer/read-order tie behavior and half-open time intervals. Handle reverse seeks, frame-based timing/fps changes, event edits, and out-of-order arrivals explicitly. The subtitle-expiry loop compares raw `stse.end` while parsed-tag expiry uses `TranslateEnd`; an incremental replacement must respect the timing mode instead of blindly sharing those raw values.

**Validation.** Counters should demonstrate one sort per changed active set, rather than per frame. Test several renders within one segment, insertion into an already selected segment, expiry at an exact boundary, and backward navigation. This is a smaller and more isolated change than finding 1.

### 4. Fuse common mask and blend paths to reduce memory traffic

**Observed.** On a bitmap miss, [DrawItem::AlphaBltDraw](../src/subtitles/draw_item.cpp#L62) obtains the overlay and clipper, fills an intermediate alpha buffer, then reads that buffer again while blending. [CompositeAlphaMask](../src/subtitles/Rasterizer.cpp#L1889) requests scratch capacity for the **entire overlay pitch × height**, even when only a narrow clipped region is processed. [Rasterizer::Draw](../src/subtitles/Rasterizer.cpp#L2013) then performs another clipping calculation and traverses the intermediate result.

The existing frame-local `AlphaMaskBuffer` already eliminates allocation per layer; proposing that again would miss the remaining cost. It still materializes the mask, and its exact-size growth can reallocate several times as larger overlays occur within a frame. Planar single-color drawing also reads the mask for four separate planes.

**Recommendation.** Add a specialized, fused kernel for single-color, unclipped body drawing first, directly converting coverage and blending into the destination. Add border/body subtraction and masked variants only when justified. A row- or tile-sized intermediate is a lower-risk alternative where fusion becomes complicated. Pass one precomputed clipped span through both stages. Reuse bounded scratch capacity across frames if measurements show allocation remains relevant.

**Validation.** Preserve 6-bit coverage, alpha conventions, saturation and integer rounding exactly. Existing paths use `alpha + 1` and `256 - alpha`; replacing these with superficially equivalent floating-point formulas can change pixels. Test zero/full/partial opacity, both alpha conventions, short and misaligned rows, karaoke transitions, border-only draws, and planar output. SSE2 paths already exist; a blanket “add SIMD” recommendation is insufficient. Measure pixels/bytes processed and cache misses, not only function samples.

### 5. Remove the unused image-sized `\be` buffer

**Observed with high confidence.** [Rasterizer::BeBlur](../src/subtitles/Rasterizer.cpp#L1230) allocates an `unsigned[(pitch+1)*(height+1)]` temporary and passes it to `be_blur`. The entire [be_blur implementation](../src/subtitles/Rasterizer.cpp#L568) never reads its `tmp_base` argument. Instead, each pass allocates and clears two width-sized `WORD` arrays and frees them at the end. The scalar helper also leaves that argument unused.

This is approximately four bytes of unnecessary temporary storage per overlay pixel, plus two small allocations per integer blur pass. The source-level waste is definite; whether all allocation overhead survives a particular optimizing compiler and how much playback it costs should be checked. It affects blur-cache misses, not already cached overlays, and the existing stress sample is primarily a `\blur` workload.

**Recommendation.** Remove the unused allocation/argument in the active `BeBlur` path, then provide reusable row scratch for all passes of one blur operation. Preserve clearing between passes. Do not remove the similarly named buffer wholesale from `OldFixedPointBlur`: its Gaussian branch passes that storage to `ass_gauss_blur`, where it is needed.

**Validation.** Use dedicated `\be` scripts covering integer/fractional strengths, mixed Gaussian/edge blur, zero border, large outlines, small images, and scale changes. Measure peak transient allocation and allocation count. This is the clearest localized waste found, but should not be advertised as a large win for the saved sample without measurement.

### 6. Treat bitmap keys and bitmap payloads as one cache policy

**Observed.** [GroupedDrawItems::Draw](../src/subtitles/draw_item.cpp#L528) first interns the group key to obtain an ID, then looks up the bitmap by that ID. On a borrowed-probe miss, [XyFlyWeight](../src/subtitles/flyweight_base_types.h#L65) copies the key and calls `Intern`, causing a second lookup. The saved 256-entry run has 126,019 bitmap misses and exactly 126,019 extra group-key queries. Groups with multiple children can also copy the probe's spilled key vector on a miss.

There is a concrete sizing inconsistency in both runtime paths. Initialization sizes **both** caches ([XySubFilter](../src/filters/transform/vsfilter/xy_sub_filter.cpp#L121), [VSFilter](../src/filters/transform/vsfilter/DirectVobSubFilter.cpp#L702)), but runtime `INT_BITMAP_MRU_CACHE_ITEM_NUM` changes resize only the bitmap cache ([XySubFilter setter](../src/filters/transform/vsfilter/xy_sub_filter.cpp#L423), [VSFilter setter](../src/filters/transform/vsfilter/DirectVobSubFilter.cpp#L2480)). If the key cache remains smaller, an evicted key receives a new ID on reuse and cannot find an otherwise retained old-ID bitmap. Increasing payload capacity alone can therefore spend memory without delivering the intended reuse.

**Recommendation.** Fix coupled resizing first. Then compare the current borrowed probe with a lookup-or-insert API that preserves the probe result and allocates only once on a miss. An integrated group-key → `{identity, bitmap}` cache is a larger option that removes duplicate lookup/eviction policy. Preserve bitmap identities while consumers retain earlier frames, and ensure any insertion token is invalidated by intervening map mutation.

Use byte/reuse-distance measurements before raising the default. The saved 256→1,024 experiment gains only 4.13 percentage points of bitmap hits, and the current borrowed-key candidate has no proven playback CPU improvement. Isolate storage changes from lookup changes when testing.

**Correctness prerequisite.** Zero is accepted as a cache capacity in the options code, but [AddHeadIfNotExists](../src/subtitles/mru_cache.h#L1102) removes its just-added entry when capacity is zero and returns that removed list position. `XyFlyWeight::Intern` then reads/writes through it. This is a source-observed invalid-position/lifetime hazard, not a newly reproduced crash. Define a real zero-capacity bypass or a documented positive minimum before using zero as an experimental “cache disabled” mode.

### 7. Hash the changing karaoke fields and improve geometry-key discrimination

**Observed.** [DrawItemHashKey::UpdateHashValue](../src/subtitles/cache_manager.cpp#L334) hashes only `m_switchpts[0]`, although equality compares all six switch-point words. [CLine::PaintAll](../src/subtitles/RTS.cpp#L1325) changes `sw[3]` as `\kf`/`\K` progresses. Different karaoke frames with otherwise identical geometry, position and primary color consequently have the same draw-key hash. Group hashing folds those hashes, propagating the collision into group interning. Full equality prevents a simple wrong-hit bug, but hash-bucket traversal and expensive comparisons can grow with retained variants.

[ScanLineData2CacheKey::UpdateHashValue](../src/subtitles/cache_manager.cpp#L178) casts scale, angle, outline and shear values to integers and packs them into a few sums. For example, different `fontShiftX` values between 0 and 1 all contribute zero. Slowly varying geometry can therefore produce many distinct keys with identical hashes.

**Recommendation.** Hash all switch-point words in their existing order; this is a small, well-defined first change. Instrument bucket-chain lengths and equality calls. Address fractional geometry only after explicitly defining its equality/canonicalization contract.

**Compatibility constraint.** Several geometry/clipper comparisons use epsilon equality, while some current hashes use raw floating representations. Blindly replacing all integer casts with exact double hashes can make keys considered equal land in different buckets. Epsilon comparison is also not generally transitive. Establish exact or canonical keys that reflect the renderer's intended geometry equivalence, preserve signed-zero behavior, and verify near-equal values before changing this system. The existing per-word retention code deliberately checks exact fields/hash compatibility; do not weaken it.

**Validation.** A progressing karaoke word should create distinct hashes for distinct switch positions while unchanged frames retain identity. Include long-held variants, fractional rotations/shears/scales, negative values, and near-equal blur/clip values. There is no substantial karaoke coverage in the main workload census, so test a dedicated script.

### 8. Reuse complete output during provably constant animation intervals

**Observed.** The current reconstruction interval avoids rebuilding `CSubtitle`, but it does not skip draw assembly/grouping. `\t` sets both animation flags ([RTS.cpp:2743](../src/subtitles/RTS.cpp#L2743)); `ParseScript` carries animation to the segment. [SimpleSubPicProvider::RenderTo](../src/subpic/SimpleSubPicProviderImpl.cpp#L262) and [XySubRenderProviderWrapper2::Render](../src/subpic/XySubRenderProviderWrapper.cpp#L454) then cache animated output for only one 100-ns tick. Subsequent distinct frame timestamps render again even before or after a step transform, when visible output may be unchanged.

**Recommendation.** Compute a safe “valid until” time for the final rendered state and use it in both adapters. Start with supported step-transform events and intersect validity across all active events and the segment boundary. If full-frame validity expires because of one animated event, event/group-level reuse from findings 1–2 can still preserve the others.

**Critical distinction.** Reconstruction stability is not output stability. Move, fade, karaoke, scroll/banner, clip changes, collision placement, and the origin workaround can change final output while the reconstructed word data is reusable. Do not simply expose `m_reuseUntil` as frame validity. Keep one-tick behavior for anything not proven constant, and invalidate on style, geometry, alpha/color-space, source or consumer changes.

**Validation.** Test immediately before, at and after every transition; simultaneous event boundaries; multiple request cadences; seeks; and source edits. Confirm both adapters render the same time semantics and reduce actual `RenderEx` calls. Benefit depends on how often all or some events are constant at once.

### 9. Avoid repeated clip work and represent inverse masks sparsely

**Observed.** [CClipperPaintMachine::CalcDirtyRect](../src/subtitles/xy_clipper_paint_machine.cpp#L35) always returns an effectively unbounded rectangle. A normal vector clip therefore cannot reduce grouping bounds there. Its `Paint` method performs another global mask-cache lookup each time rather than retaining the result for the machine's lifetime.

An inverse vector clip uses [PaintBaseClipper](../src/subtitles/RTS.cpp#L1010), which allocates and initializes a full `m_size.cx × m_size.cy` mask even when the vector affects a small region. The simple noninverse path already shares a compact overlay mask and should be preserved.

Inverse rectangular clipping has a separate multiplier: [RenderOneSubtitle](../src/subtitles/RTS.cpp#L3815) paints every line once for each of four exterior rectangles. It creates slots and paint machines even for empty exterior rectangles. The per-word draw-key cache retains three keys ([GetDrawItemHashKey](../src/subtitles/xy_overlay_paint_machine.cpp#L49)), so four distinct region clips can defeat that retention even for a body-only word; adding outline/shadow makes the working set larger.

**Recommendation.** Share one word/layer geometry plan across inverse regions and avoid clearly empty regions before object construction, subject to grouping equivalence. Cache a clipper's immutable painted mask per frame/machine. Use actual bounds for normal vector clips. Represent inverse clips as a compact exception mask with an implicit fully allowed exterior, and represent scroll/banner attenuation analytically or in compact spans where practical.

**Validation.** Preserve current clip rounding, even-coordinate chroma expansion, offscreen behavior and blur extents. An inverse mask's small vector bounds are **not** the bounds of its allowed region. Reducing invisible items can change `DecideDrawMethod` and bitmap partitioning, so compare final composition as well as individual bitmap metadata. Count mask bytes and mask-cache queries on scripts with more than the default 48 unique clip masks.

### 10. Bound retained and transient memory by ownership and bytes

**Observed.** Most raster caches are bounded by item count. [ShrinkCache](../src/subtitles/RTS.cpp#L3299) responds to `g_xy_malloc_used_size` by removing tails from only the no-blur, overlay and bitmap caches, one from each per loop. That counter tracks [xy_malloc allocations](../src/subtitles/xy_malloc.cpp#L16), not all `new`, STL/ATL storage, parsed commands, path arrays or GDI resources. Full-frame clip masks use `xy_malloc` but their cache is not among the three evicted caches. Shared owners can also keep image allocations alive after their cache entries are removed.

The current 16 MiB syntax admission budget, 32 MiB reconstruction accounting, and 1 MiB event pool are useful separate bounds, but they are not a total process-memory ceiling. Frame scratch is created and released per composition and grows to the largest overlay requested, independently of visible clipped area.

**Recommendation.** Add per-cache retained-byte and transient-byte reporting, including owner-retained images. Use coordinated byte budgets with measured reuse/cost rather than indiscriminately evicting one entry from each cache. Cache-owned strong references introduced by other optimizations must have a bounded lifetime and a clear charge; otherwise an apparent MRU eviction will not reclaim their memory. Prefer bounded renderer-owned scratch with growth limits over process-global pools.

**Validation.** Exercise large resolution changes, large blur extents, many unique clippers, seeks, stream switches and end-of-file. Track accounted bytes, process private bytes, peak transient bytes and actual releases separately. Optimize tail latency under memory pressure as well as average rendering time.

## Additional areas and constraints

### Subtitle ingestion can be expensive independently of rendering

[CSimpleTextSubtitle::Add](../src/subtitles/STS.cpp#L2593), used by embedded text samples, splits segments and copies active-event arrays. [CreateSegments](../src/subtitles/STS.cpp#L3157) also materializes the event membership of each interval. Many overlapping events can require large total membership storage. A sweep over start/end changes with persistent/shared active sets is a possible redesign, and append-friendly ingestion could reduce segment-array movement. Profile subtitle-pin delivery and load/seek time before selecting this work: offline `RenderEx` timing does not measure incremental embedded delivery, and materializing all memberships has an unavoidable cost proportional to their number. This is a secondary opportunity, not evidence that ingestion causes the observed playback drops.

### Parallel rendering requires an ownership redesign first

Rendering is synchronous under filter/provider locks, and the repository consumer assumes `RequestFrame` immediately calls `DeliverFrame` on the same thread. The implementation also has a shared [GDI DC](../src/subtitles/RTS.cpp#L53), [global cache collection](../src/subtitles/cache_manager.cpp#L488), [mutable default frame creator](../src/subtitles/xy_bitmap.cpp#L384), unlocked flyweights, and a non-atomic allocation counter. Adding a parallel loop around events or cache access is unsafe.

A possible later design is serial evaluation/group planning followed by parallel pixel work for sufficiently large, independent output rectangles, with immutable inputs and worker-local scratch. Preserve draw order inside each group. Resolve shared state, failure handling and scheduling overhead before benchmarking. It is not the first optimization to implement while avoidable serial work remains.

### Cache invalidation needs a focused correctness check before longer reuse

In [XySubRenderProviderWrapper2::RequestFrame](../src/subpic/XySubRenderProviderWrapper.cpp#L338), output/alpha changes set `should_invalidate = true`, but the later existing-frame check **assigns** `should_invalidate = (count > m_max_bitmap_count2)`. A frame within the bitmap-count limit can overwrite the earlier invalidation decision. The local source permits stale static-frame reuse through this path; a reproduction should drive this wrapper directly and also check whether higher-level callers mask the issue. Accumulate independent invalidation reasons rather than replacing one with another before extending frame validity.

### Work to deprioritize

- Further wholesale polygon-number parsing or raw-path rewrites for this sample: current path reuse is already high. Complex continuously deforming polygons still deserve a separate workload.
- Blanket overlay-cache enlargement: a 99.91% hit rate leaves little miss work to eliminate here, while millions of successful lookups still cost time.
- A new text-shaping engine, GPU renderer, approximate blur, reduced subpixel precision, or skipped animation frames as a “transparent optimization”: each expands compatibility/product scope and may change output.
- Generic compiler-flag tuning before the above work: release shared code already uses maximum-speed optimization and inlining. Keep toolchain comparisons separate, particularly since saved local builds use v145 while the checked-in CI path targets VS2019.

## Suggested implementation sequence

1. Establish a repeatable control for the current x86/x64 grouping experiment and add phase/counter measurements. Keep inline storage and borrowed-key lookup as separate candidates.
2. Make localized, independently reviewable changes: coupled runtime cache sizing and zero-capacity handling; karaoke hash coverage; removal of unused edge-blur scratch. Run focused correctness gates for each, and use matching workloads to measure their value.
3. Cache active-set ordering and expiry work, then replace remaining temporary grouping lists with compact frame storage. Keep output partitioning unchanged initially.
4. Prototype dependency-based reuse for a conservative class of animated events. Extend to multiword stable states and safe output-validity intervals only after measuring rejection reasons and preserving semantics.
5. Optimize common composition kernels and clipped working sets based on the resulting profile. Reconsider grouping topology, ingestion data structures and parallelism only when earlier work no longer dominates.

## Validation plan for later implementation

No checks in this section were run as part of this review. The saved prior gates do not validate any proposed change.

**Functional gates.** Use the existing [native harness](../test/benchmark/render_subtitles.cpp) and [benchmark instructions](../test/benchmark/README.md): 13 focused contracts in the current candidate, 54 fixture cases, the complete 2,428-frame workload, and all six extended scripts. Add targeted cases for progressing karaoke, fractional-key collisions, zero/runtime cache capacity, multiword continuous transforms, clip working-set overflow, `\be`, same-segment insertion, and adapter invalidation. Test Win32 and x64 and both runtime paths.

The native comparator hashes positions, dimensions and visible RGBA bytes. That is a strong gate for changes that preserve bitmap partitioning. An intentional grouping/partitioning redesign may produce equivalent final pixels with different bitmap counts or bounds; in that case add an exact final-composite image comparison and verify clipping/bitmap identity/lifetime contracts instead of declaring any hash difference acceptable. Include overlapping translucent layers and background composition. Also cover the VSFilter pixel formats and alpha conventions absent from RGBA-only native checks; the general unit-test entry point currently selects only `test_overall.h`.

**Instrumentation.** Record per-frame time for ingestion separately from segment maintenance, event evaluation/layout, draw construction, grouping/key lookup, raster/blur misses, mask/composition, and consumer-side copying/blending. Add allocation bytes/counts, cache chain/equality counts, group membership sizes, clip area versus overlay area, and reconstruction/validity rejection reasons. Use counters in diagnostic builds and account for their overhead.

**Performance gates.** Compare identical fixed work using mean, p95/p99 and frames above the source-frame budget, then paired unprofiled playback with the same resolution, renderer, engine architecture, fonts, track, cache settings and polling cadence. Keep builds and offline tests separate from playback. Preserve matching DLL/PDB/source manifests. Do not pool the older madVR/smaller-window runs with the later MPC Video Renderer/full-HD runs. Include initial-interval drops and exclude interrupted or invalid-counter captures explicitly.

For playback, use only the required Debug (`LOCAL`) `Z:\zpAI\zplayer.exe` and helpers, the sample's default subtitle track, and TCP `1150` statistics. The paused-work record notes unavailable `Z:` access; this review neither tested that availability nor resumed playback. Fresh measurements require the working benchmark environment and a request to proceed with implementation/testing. No new software is needed for the review or proposed validation workflow.

## Implementation follow-up (2026-10-26)

The first localized batch now implements coupled bitmap/key capacity, safe
zero-capacity bypass, all six karaoke hash words and reusable edge-blur rows.
Both architectures build and pass all pixel gates. Eight matching Debug Zoom
Player captures show small observed CPU reductions but overlapping drop ranges;
zero drops is not achieved. See the [implementation and measurement report](performance-20261026-review-implementation.md).
The larger event/layout, ordering/expiry, grouping and validity work remains.
