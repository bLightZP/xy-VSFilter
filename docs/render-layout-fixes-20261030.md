# Render layout and allocation fixes — 2026-10-30

Implements the code-confirmed findings from the [review](F:/Git/xySubFilter/docs/render-layout-and-transparency-review-20261030.md). The sample-dependent transparency flicker investigation remains deferred. These changes do not establish a cause for the reported madVR error or flicker.

**Configuration and dimensions**

- Both More pages stage edits until Apply/OK. An unrelated control cannot commit a partially typed resolution. Invalid input selects the offending edit, displays an edit-control validation balloon, and returns `E_INVALIDARG` without applying or persisting layout changes.
- Custom width and height must each be 16–12,800. The upper bound preserves the former UI range; the lower bound removes accidental single-digit configurations. Renderer-side scale/work checks provide separate protection, so the UI minimum is not the sole defense.
- The same custom-size validation runs before API mutation and when loading registry settings. Invalid saved dimensions recover to 1280×720. A new appended `BIN2_RENDER_LAYOUT` option atomically commits mode, dimensions, and render-to-original policy under the settings lock, with one layout invalidation. Existing option IDs are unchanged.
- “Render To Original Video Size” uses `SIZE_ORIGINAL_VIDEO`; custom layout dimensions remain a separate input to the renderer. For a 1080p source, selecting an 8K layout no longer turns this checkbox into an 8K output request. Explicit 8K consumer output remains supported.

**Frames, memory, and failures**

- The native Xy wrapper no longer constructs two full-target combining buffers when dimensions change. Combining allocates an owned bitmap only when needed, sized to the clipped union of the source bitmaps. Frames retained by a consumer continue owning their pixels independently.
- Combined output preserves inverse alpha or ordinary opacity as negotiated, including transparent gaps and color-space metadata. Fully outside source bitmaps are safely skipped by packed copying.
- Static-frame reuse is invalidated for all relevant geometry, layout, FPS, bitmap-limit, alpha, and original-size changes. Both VSFilter and XySubFilter invalidate on layout updates.
- XySubFilter schedules optional `ISubRenderConsumer2::Clear(0)` on its existing worker. The callback runs outside filter, provider, and pending-state locks. Requests coalesce; invalidation during Clear and an older frame finishing delivery after Clear schedule further work. Consumers without the extension remain supported. Full clearing avoids mixing subtitle-time invalidation boundaries with the consumer's video timeline.
- Bitmap and pooled-buffer byte counts are checked before multiplication/allocation. `xy_malloc` rejects alignment-header overflow. Allocation failures in bitmap creation, Gaussian coefficients, Gaussian scratch/transposition, edge blur, and the libass adapter propagate without dereferencing null storage.
- The renderer catches allocation failures at its COM boundary. Temporary frame construction and animated-event cleanup have scoped ownership. Failed blur results are not published to paint machines or cached as unblurred substitutes. The wrapper preserves failure HRESULTs, releases partial frames, and unlocks its provider.

The renderer admits positive geometry up to 32,768 pixels per dimension, output-to-layout scale up to 64 per axis, and individual checked bitmap/scratch requests up to 256 MiB. Gaussian sigma is bounded to 1,365 per axis (kernel width at most 4,095); edge-blur passes/padding are bounded to 4,096, with a padded-pixel work check of 2^32 filter/pass units. These are defensive admission limits, not memory reservations or a process-wide memory ceiling. Unsupported work fails rather than entering enormous allocations or blur loops. Already retained consumer frames and other renderer allocations remain outside the per-request byte limit.

The existing syntax, reconstruction, drawing, lookahead, grouping, cache, and SIMD composition optimizations remain in place. Changes in the native render body are validation and failure/ownership handling; the algorithms used for admitted successful renders are unchanged.

**Regression coverage**

The new `--layout-contract` covers wrapper dimension selection, static reuse/invalidation, FPS changes, bounded combining at an 8K target, both alpha conventions, 128 retained frames, allocation faults, oversized dimensions, tiny-layout rejection, failed-blur publication, and queue clearing with reentrant invalidation and late delivery. It also renders a small ASS drawing using native and libass through independent 1080p/8K output/layout combinations, then verifies allocation-failure recovery against each backend's own reference output.

`--layout-filter-contract xy|vs <DLL>` loads the actual DLL/property page and redirects that process's HKCU accesses to a dedicated test subtree. It checks bad saved settings, direct API rejection, atomic transaction rejection, partial typing, an unrelated control notification, invalid Apply, successful Apply/persistence, and closing without Apply. Normal user subtitle settings are not changed. The test runner uses subprocess timeouts so a regression cannot leave an unattended hardlock.

**Validation results**

- Release builds succeeded for `VSFilter`, `XySubFilter`, `xy_sub_filter_consumer`, and the console rendering harness on both x64 and Win32, using the installed v145 toolset.
- All three new layout contracts and all 34 existing renderer contracts passed on each architecture: 74 successful contract runs in total.
- All 54 rendering-fixture rows matched the control on each architecture. The extracted default track from `Subtitles_CPU_killer_on_end_credits.mkv` also matched across all 2,428 tested frames on each architecture, with zero bitmap-placement or pixel-hash mismatches.
- The control executables were verified byte-for-byte against the archived v3.2.1.910 release at baseline commit `755b162c6ea944418ee276b57df559dfa4eca810`. Candidate binaries include the uncommitted changes described here.
- Whitespace validation passed with the repository's CRLF convention (`git -c core.whitespace=cr-at-eol diff --check`).

One serial control/candidate credits run per architecture produced these offline render times:

| Architecture | Control mean / p99 | Candidate mean / p99 |
| --- | --- | --- |
| x64 | 10.226 / 41.105 ms | 10.370 / 41.466 ms |
| Win32 | 10.251 / 38.851 ms | 10.158 / 38.416 ms |

These timings are a smoke check, not statistical evidence of a performance change or an end-to-end playback benchmark. Existing performance optimizations remain in place, and the tested rendering output is unchanged.

Evidence is recorded under `benchmark-results/layout-fixes-20261030`: [build hashes](F:/Git/xySubFilter/benchmark-results/layout-fixes-20261030/build-manifest.json), [x64 focused tests](F:/Git/xySubFilter/benchmark-results/layout-fixes-20261030/focused-x64.json), [Win32 focused tests](F:/Git/xySubFilter/benchmark-results/layout-fixes-20261030/focused-Win32.json), [x64 renderer contracts](F:/Git/xySubFilter/benchmark-results/layout-fixes-20261030/contracts-x64.json), [Win32 renderer contracts](F:/Git/xySubFilter/benchmark-results/layout-fixes-20261030/contracts-Win32.json), [x64 frame comparison](F:/Git/xySubFilter/benchmark-results/layout-fixes-20261030/pixels-x64.json), and [Win32 frame comparison](F:/Git/xySubFilter/benchmark-results/layout-fixes-20261030/pixels-Win32.json).

Playback was not run and no DLL was installed or registered. Consumer queue clearing was verified with the regression mock; real madVR behavior and the sample-dependent flicker investigation remain to be checked when the samples are available.
