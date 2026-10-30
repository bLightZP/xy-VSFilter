# Render layout, resolution, and transparency review — 2026-10-30

Review baseline: commit `755b162c`, version 3.2.1.910. This is a source review and proposed implementation plan; no renderer changes, playback reproduction, or new performance measurements were made. The reported samples and madVR error are unavailable. The earlier ASS spike benchmarks cover a different workload and do not establish the cause of these reports.

The code confirms that unfinished resolution edits can be applied and persisted, and that “Render To Original Video Size” uses the selected **layout size**, including a custom 8K size, as the actual output size. These are the first issues to address. Additional allocation, invalidation, and alpha-contract defects merit focused fixes. None of those findings, by itself, proves the cause of the reported transparency flicker.

| Priority | Finding | Confidence and connection to report |
| --- | --- | --- |
| P1 | Every edit can apply the entire property page and persist an intermediate width of `1`; validation is insufficient downstream. | Confirmed code path. A plausible route to excessive rendering work; the exact reported hardlock is not reproduced. |
| P1 | The “original video size” option substitutes the selected layout size for the raster size. An eager combining pool magnifies the memory cost. | Confirmed size selection and allocation behavior. Strong explanation for the reported performance discontinuity. |
| P1 | Bitmap/blur allocation failures and size arithmetic are not consistently handled. | Confirmed robustness defects. Plausible crash contributors under the above workloads; no crash attribution without a dump. |
| P2 | A wrapper can discard a required invalidation; setting changes do not clear already delivered consumer frames. | Confirmed stale-output paths. Relevant to settings, track changes, and late subtitle packets, not proof of steady-state fade flicker. |
| P2 | Bitmap combining does not preserve the negotiated destination-alpha convention. | Confirmed conditional contract defect. Test with `useDestAlpha=true` and combining active; ordinarily does not explain the libass report. |

**1. Stop committing partially typed resolutions, and validate before changing state**

The shared property-page handler invokes `OnApplyChanges()` for `EN_CHANGE`, `CBN_SELCHANGE`, and `BN_CLICKED` when instant update is enabled, which is the default. It does this before dispatching to the page-specific `OnMessage()`. Applying deactivates the page, transfers its settings, calls `UpdateRegistry()`, and reactivates it. Thus a handler added only to the derived page is too late to stop the existing auto-apply path. See [base message handling](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSubPropPage.cpp:257) and [apply/persistence](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSubPropPage.cpp:355).

Both More pages accept spinner values from 1 to 12,800. Their control reads use `GetPos32()` without checking its error output. The XySubFilter page writes layout mode, then custom dimensions, then other options including the original-size flag; this is not one validated settings transaction. Errors are logged, while the outer Apply operation still returns success. See [VSFilter limits](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSubPropPage.cpp:1213), [XySubFilter setting transfer](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSubPropPage.cpp:2072), [dimension reads](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSubPropPage.cpp:2168), and [XySubFilter limits](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSubPropPage.cpp:2264).

The settings API validates the layout-mode enum but does not validate the custom size. The generic setter stores the size before calling `OnOptionChanged()`, so rejecting it from that callback would not undo the mutation. Both registry loaders replace nonpositive dimensions with 1280×720, but accept `1` and arbitrary large positive values. See [option validation](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSub.cpp:863), [setter ordering](F:/Git/xySubFilter/src/filters/transform/vsfilter/XyOptionsImpl.cpp:278), and [registry loading for VSFilter](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSub.cpp:1190) / [XySubFilter](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSub.cpp:1612).

There is a concrete expensive-work mechanism behind the small-size concern. Native renderer initialization computes `target_scale_x = output_width / layout_width`; it has only an assertion against zero, after multiplying geometry into subpixel coordinates. Gaussian blur multiplies its strength by this scale, expands the intermediate bitmap, and constructs a kernel from the scaled strength. For example, assuming no script layout override, output width 1920, layout width 1, and `\blur3`, the horizontal sigma becomes 5760 and the kernel width becomes 17,281. This is a source-derived illustration, not a measurement from the missing sample. See [renderer initialization](F:/Git/xySubFilter/src/subtitles/RTS.cpp:2538), [kernel construction](F:/Git/xySubFilter/src/subtitles/Rasterizer.cpp:106), and [scaled blur/allocation](F:/Git/xySubFilter/src/subtitles/Rasterizer.cpp:1084).

Rendering and option updates also share the XySubFilter filter lock. A very expensive render can therefore leave the property page waiting for that lock. The evidence supports runaway work or allocation pressure as possibilities; it does not establish a lock-cycle deadlock. See [lock setup](F:/Git/xySubFilter/src/filters/transform/vsfilter/xy_sub_filter.cpp:55) and [frame requests](F:/Git/xySubFilter/src/filters/transform/vsfilter/xy_sub_filter.cpp:1181).

Proposed change:

1. Stage layout mode and both dimensions in the page. Commit the complete group on explicit Apply/OK. The simplest first patch is to disable instant update for the affected More pages; if other controls retain instant update, they must not accidentally commit unfinished size edits through the full-page apply path. A typing debounce does not solve a user pausing at `1`.
2. Parse and validate both fields explicitly. Preserve the last accepted settings while editing, show an actionable validation error, and propagate failure from Apply without persisting partial state. Cancel must discard staged edits.
3. Validate at the option boundary **before mutation**, and use the same rules when loading persisted settings. Replace unsafe saved settings with a known-safe default so opening the filter does not require manual registry repair.
4. Establish a documented supported range plus checked geometry, scale, blur-work, and allocation limits. Raising the minimum to `2` alone would leave the underlying problem. Bounds must protect rendering through direct API calls and script-provided layout overrides as well as the UI. Validate before destroying the current renderer state.
5. Commit mode, dimensions, and dependent render-size policy under one settings update, with one invalidation. Keep 8K available where supported; a blanket ban would obscure the actual size-selection and memory problems.

**2. Separate logical layout dimensions from actual output dimensions**

`XySubRenderProviderWrapper2::RequestFrame()` reads `SIZE_LAYOUT_WITH` into a variable named `original_video_size`. That option is a selector: it may return original video dimensions, aspect-adjusted dimensions, or the custom layout dimensions. With the checkbox enabled, the wrapper replaces both output and subtitle target rectangles with this selected size. Actual original-video dimensions exist separately as `SIZE_ORIGINAL_VIDEO`. See [size selection](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSub.cpp:832), [wrapper override](F:/Git/xySubFilter/src/subpic/XySubRenderProviderWrapper.cpp:318), and [consumer original-video dimensions](F:/Git/xySubFilter/src/filters/transform/vsfilter/xy_sub_filter.cpp:1976).

For a consumer requesting 1920×1080 output, a 1920×1080 original video, and a custom 7680×4320 layout, the current behavior is:

| Checkbox | Selected logical layout | Actual wrapper output/target | Native target scale, absent a script layout override |
| --- | --- | --- | --- |
| Off | 7680×4320 | Consumer rectangles, assumed 1920×1080 here | 0.25 horizontally and vertically |
| On | 7680×4320 | 7680×4320 | 1.0 horizontally and vertically |

The second case has 16 times the full-surface pixel area and four times the target scale in each axis. Scale-dependent blur and intermediate padding can add further work. This is not a prediction of a 16× frame-time increase: actual bitmap bounds, effects, and caching matter. Nor does selecting an 8K layout with the checkbox off prove that the filter was rasterizing at 8K. Actual consumer rectangles must be captured; a 1080p source does not itself establish the consumer's output resolution. A script `LayoutResX/Y` can also override the renderer's effective layout dimensions; see [RenderEx layout selection](F:/Git/xySubFilter/src/subtitles/RTS.cpp:4775).

There is an avoidable allocation cost even when no bitmap combining is required. A target-rectangle change constructs a `CPooledSubPicAllocator` with capacity two. Its constructor immediately allocates two full-target buffers. At 7680×4320×4 bytes, that is **126.56 MiB per buffer, 253.13 MiB for the initial pair**, excluding native bitmaps, blur scratch, and consumer/GPU resources. The pool grows when all its buffers are retained. See [eager pool creation](F:/Git/xySubFilter/src/subpic/XySubRenderProviderWrapper.cpp:365), [pool initialization](F:/Git/xySubFilter/src/subpic/PooledSubPic.cpp:26), [growth](F:/Git/xySubFilter/src/subpic/PooledSubPic.cpp:137), and [buffer allocation](F:/Git/xySubFilter/src/subpic/PooledSubPic.cpp:222).

A 128-frame consumer queue makes ownership and allocation size consequential. Hypothetically, retaining 128 distinct full-size 8K CPU buffers alone would consume 15.82 GiB. This is a capacity illustration, **not** a claim that madVR retains that exact set of buffers; consumer behavior and bitmap combining determine what is actually held.

Proposed change:

1. Carry distinct `originalVideoSize`, `layoutSize`, `outputRect`, and `subtitleTargetRect` values. Make “Render To Original Video Size” use actual original-video dimensions while passing the selected layout separately. If rendering at the custom layout resolution is an intended feature, expose it with an explicit name and policy instead of conflating it with original-video size.
2. Create combining storage only after a frame exceeds the negotiated bitmap-count limit. Prefer storage sized to the combined dirty bounds, preserving frame coordinates and clipping metadata. Measure that change separately from the checkbox correction.
3. Account for retained-frame bytes and allocation peaks. Never recycle or modify a buffer still owned by the consumer. Use bounded reuse and a defined allocation-failure policy rather than assuming the initial two buffers bound memory use.
4. Add diagnostic fields for selected/effective layout, actual original video, output/target rectangles, bitmap bounds, allocated bytes, and combining. These distinguish a performance regression from an unintended increase in render resolution.

**3. Make allocation and render failures recoverable**

Several paths can turn high memory demand into a crash or inconsistent output:

- `XyBitmap::CreateBitmap()` computes byte sizes with signed integer products, accepts a null `xy_malloc()` result, and calls `ClearBitmap()`, which writes through the resulting pointer. This bitmap code is used by native rendering and the libass adapter. See [bitmap allocation and clearing](F:/Git/xySubFilter/src/subtitles/xy_bitmap.cpp:43).
- `GaussianBlur()` passes its first scratch allocation directly into `xy_gaussian_blur()` without checking for null. Its later body allocation does check. `Rasterizer::Blur()` only asserts the return values of its blur operations and then returns true, hiding failures in builds without assertions. See [failure propagation](F:/Git/xySubFilter/src/subtitles/Rasterizer.cpp:1059) and [unchecked scratch allocation](F:/Git/xySubFilter/src/subtitles/Rasterizer.cpp:1125).
- The Xy wrapper asserts success from `RenderEx()` but proceeds, potentially replacing its HRESULT with a later bitmap-count or combining result. This needs explicit error handling and guaranteed provider unlock, not reliance on assertions. See [wrapper rendering](F:/Git/xySubFilter/src/subpic/XySubRenderProviderWrapper.cpp:419).

Use checked `size_t` arithmetic for dimensions, stride alignment, padding, and byte counts, with checks before casts back to integer geometry. Reject negative, overflowed, nonfinite, and unsupported work sizes before allocation. Check every allocation used by the affected path, propagate failures without caching incomplete output, and clean up through scoped ownership. Define a bounded failure behavior that keeps video playback responsive and does not repeatedly retry the same impossible allocation without a state change. Use fault injection to test this; do not reproduce by exhausting the user's machine.

These are independent correctness fixes. They make the reported crash plausible under memory pressure but do not identify the particular madVR error.

**4. Preserve invalidations and notify queued consumers**

The wrapper sets `should_invalidate=true` when layout, alpha mode, output, or target geometry changes. If a cached frame exists, it then **assigns** `should_invalidate = (count > maxBitmapCount)`, discarding the earlier result. For a cached static frame within its valid interval, `Render()` returns that frame unchanged. The stored settings have already been updated, so the original mismatch will not necessarily be detected on the next call. Preserve accumulated invalidation reasons instead. See [overwritten invalidation](F:/Git/xySubFilter/src/subpic/XySubRenderProviderWrapper.cpp:338) and [static-frame reuse](F:/Git/xySubFilter/src/subpic/XySubRenderProviderWrapper.cpp:419).

The original-size checkbox also has no case in XySubFilter's `OnOptionChanged()`. Other layout changes invalidate the local provider, but `InvalidateSubtitle()` does not call the consumer's queue-clearing extension, despite its log claiming provider and consumer were cleared. The only `m_consumer->Clear()` call in this file is commented out. Changing context/bitmap IDs affects newly delivered frames; it does not withdraw previously delivered frames. See [option handling](F:/Git/xySubFilter/src/filters/transform/vsfilter/xy_sub_filter.cpp:350), [provider-only invalidation](F:/Git/xySubFilter/src/filters/transform/vsfilter/xy_sub_filter.cpp:1918), and the local [ISubRenderConsumer2::Clear contract](F:/Git/xySubFilter/include/SubRenderIntf.h:298).

The VSFilter path needs equivalent attention: its option-change switch lacks custom-layout size/mode invalidation, while `SimpleSubPicProvider` can reuse a frame over a static subtitle's interval. See [VSFilter option handling](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSubFilter.cpp:2261) and [simple-provider reuse](F:/Git/xySubFilter/src/subpic/SimpleSubPicProviderImpl.cpp:175).

Proposed change:

1. Accumulate all invalidation reasons; include the original-size flag and effective FPS/geometry/alpha dependencies. Invalidate both adapters consistently when their rendering inputs change.
2. Query the optional `ISubRenderConsumer2` interface and request clearing at the appropriate timeline boundary for settings, track, or late-packet invalidation. Support consumers that expose only the original interface.
3. Dispatch external consumer calls outside filter/provider locks. `Clear()` permits fresh requests, so a direct call from the currently locked invalidation routine risks reentrancy or lock-order trouble. Use a generation/pending-notification design that cannot lose a concurrent invalidation.
4. Preserve the immutability of already delivered frame objects, even when clearing caches. The [frame lifetime contract](F:/Git/xySubFilter/include/SubRenderIntf.h:287) explicitly permits consumer buffering.

These fixes can eliminate stale output after changes or late packets. An ordinary fade with unchanged settings and timely input requires further investigation; it is not explained just by finding a queue-invalidation defect.

**5. Preserve alpha conventions when combining bitmaps**

The wrapper selects `MSP_RGBA` when `useDestAlpha=true`, otherwise `MSP_RGBA_F`. In this code those use inverse alpha and ordinary opacity respectively; see [format definitions](F:/Git/xySubFilter/src/subpic/ISubPic.h:37). However, `CombineBitmap()` clears the destination to `0x00000000`, copies source pixels without alpha conversion, and returns a frame wrapper whose color space is always `XY_CS_ARGB_F`. With inverse-alpha input, transparent holes require an alpha byte of 255, and the returned format must match the actual bytes. See [format selection](F:/Git/xySubFilter/src/subpic/XySubRenderProviderWrapper.cpp:433), [combining](F:/Git/xySubFilter/src/subpic/XySubRenderProviderWrapper.cpp:499), and [fixed output color space](F:/Git/xySubFilter/src/subpic/XySubRenderFrameWrapper.cpp:54).

This combination is reachable: the repository's XySubFilterConsumer explicitly requests `useDestAlpha=true`. Combining activates when the native frame exceeds the negotiated bitmap-count limit. See [consumer negotiation](F:/Git/xySubFilter/src/filters/transform/vsfilter/xy_sub_filter_consumer.cpp:869).

Preserve the negotiated alpha convention through clearing, copying, and metadata, or convert consistently at an explicit boundary. Test both modes, including transitions across the bitmap-count threshold and transparent gaps within the combined bounding rectangle. Do not apply a blanket alpha inversion to every frame.

This defect is a credible source of native transparency artifacts under those conditions. The current libass adapter emits at most one combined bitmap per frame, so ordinary libass output does not take this multi-bitmap combining path. Consequently, it cannot by itself account for the report that both backends flicker. See [libass rendering and compositing](F:/Git/xySubFilter/src/subtitles/LibassRenderer.cpp:176).

**Investigation plan for the unresolved flicker**

First verify the actual backend. “Prefer libass” is a preference; source type and supported input determine whether it is used. Record the selected backend on frame delivery rather than infer it from the checkbox. A shared symptom makes the common wrapper, timing, buffering, and alpha negotiation useful investigation points, but does not prove a shared root cause.

Add a focused synthetic ASS suite now, without waiting for the user's samples:

- `\fad`, `\fade`, animated `\alpha`, separate primary/outline/shadow alpha, overlapping layers, clipped fades, blurred edges, and transitions through fully transparent output.
- Exact event/fade endpoints and timestamps immediately before/after them; sequential playback, backward seeks, and future requests retained in a queue.
- Native and libass, each through direct rendering and the full Xy wrapper/consumer route. Compare each backend with its own uncached reference and expected alpha progression; do not require pixel identity between different renderers.
- `useDestAlpha` false/true; combining disabled/forced by bitmap-count limits; 0/1/multiple bitmaps; retained frame contents and metadata unchanged after subsequent renders.

Capture request and render timestamps, effective backend, generation/context, bitmap IDs, bounds, pixel/alpha hashes, and delivery intervals in a bounded diagnostic trace. The current Xy request path renders at the request start; comparisons must use the same timestamp rather than silently comparing it with a frame midpoint. If direct-render pixels already flicker, investigate that backend's parsing/evaluation/composition. If pixels remain correct until wrapping or delivery, investigate IDs, alpha, invalidation, or timing. If delivered bytes remain correct, the consumer/display stage becomes the next boundary to inspect.

When samples arrive, obtain the ASS/SSA data and fonts or a short source clip, exact affected timestamps, loaded DLL version/architecture, the exact madVR error, actual output dimensions, and both-backend settings. The existing CPU-killer credits file can remain a performance regression workload, but cannot substitute for the missing transparency sample.

**Implementation sequence and acceptance checks**

| Stage | Work | Required verification |
| --- | --- | --- |
| 1 — safe configuration | Stage layout edits; validate API/registry/renderer inputs; commit settings coherently; propagate Apply errors. | Slow input sequence `empty → 1 → 19 → 192 → 1920`, height edits, backspace/paste, tab, Cancel, Apply, and clicking another control while editing. No intermediate render/persistence. Bad saved values recover safely. Direct API rejection leaves prior state intact. |
| 2 — correct dimensions and memory | Separate layout/output/original sizes; fix checkbox semantics; allocate combining buffers lazily; checked allocation and failure handling. | Original video 1080p with 1080p and 8K layouts; consumer output 1080p and 8K; checkbox both ways; native and libass. Assert exact frame rectangles and allocation sizes. No combining pool allocation when combining is unused. Forced allocation failures return cleanly. |
| 3 — frame contracts | Fix accumulated invalidation, both filter option paths, queue notification, and combining alpha. | Static subtitle survives settings changes only as a newly correct frame; mocked consumers with/without Consumer2; reentrant requests during Clear; retain 128 old frames and verify immutability; alpha/bitmap-count transition matrix. |
| 4 — flicker localization and performance | Synthetic fade suite, bounded traces, then user samples; rerun established spike workloads after behavior fixes. | Locate the first stage where output diverges. Measure wall/CPU tail latency, allocation peaks, bitmap count, actual raster area, and queue misses separately. Preserve supported ASS behavior and avoid relying on queue depth to conceal sustained render overload. |

The existing [libass contract](F:/Git/xySubFilter/test/benchmark/render_subtitles.cpp:1887) covers useful renderer behavior, while the [filter contract](F:/Git/xySubFilter/test/benchmark/render_subtitles.cpp:1947) exercises preference options and the Main page. They do not cover the More-page edit transaction or the external wrapper/consumer matrix above. The ordinary console render loop also [fixes output and layout to 1920×1080](F:/Git/xySubFilter/test/benchmark/render_subtitles.cpp:2055), so its current results cannot validate the checkbox/layout distinction.

Extend the harness with independently configurable original, layout, output, and target sizes and a mocked consumer. Test registry recovery through an isolated settings store, allocation failures through fault injection, and pathological tiny-layout rendering in a child process with a watchdog. Run the relevant existing rendering/composition contracts on x86 and x64 after implementation. Any playback benchmark must use the required Debug (`LOCAL`) Zoom Player build and the repository's [benchmark procedure](F:/Git/xySubFilter/test/benchmark/README.md); no playback benchmark was performed during this review.
