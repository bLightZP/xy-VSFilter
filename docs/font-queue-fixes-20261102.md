# Native font realization and EOF queue fixes

Implementation follow-up to the [sample investigation](brokensubs3-and-loop-queue-review-20261102.md).
These fixes are included in [v3.2.1.960](releases/v3.2.1.960.md).
Initial implementation checks below used the candidate before its version-resource rebuild.

## Changes

`CMyFont` bounds GDI realization at height 15087 and retains the corresponding
scale. `CText` restores measured advances and glyph coordinates to the authored
size before the existing layout/transform path. Character spacing is added
after glyph rescaling so it is not enlarged a second time. This avoids the CFF
font failure triggered by high custom subtitle resolutions without reducing the
intended subtitle size.

Font metrics are initialized and API results checked. Failed measurements do
not enter the text-info cache. Selected fonts are restored on all exit paths;
failed outline operations abort and discard partial paths. Rescaling happens
when building native metrics/outlines, not per pixel or per animation frame.
Normal-size fonts retain the original coordinate rounding.

The authored style and native cache keys remain unchanged. Prepared layouts,
reconstruction reuse, outline widening, bitmap caches and alpha evaluation
remain in place. Libass font realization and rendering are unchanged. Both
VSFilter and XySubFilter share the native font correction.

`SubtitleInputPin2::NewSegment` now calls a dedicated selected-stream reset in
XySubFilter. It invalidates the local provider and advances the consumer
invalidation epoch, retiring the previous request horizon and notifications.
It does not send `Clear(0)` during the graph's own seek/flush transition. That
callback was causing madVR to re-request the last EOF frame before switching
timelines, reinstating the old horizon and repeatedly clearing the new queue.

User-setting changes still clear consumer output. Genuinely late subtitle
packets still produce coalesced partial clears, with delay/rate conversion and
in-flight delivery protection. The separate VSFilter local queue and bundled
synchronous consumer were inspected and retain their existing behavior.

## Regression coverage

`run_font_queue_regressions.py` runs six bounded tests per architecture:

- CFF/TrueType glyph-size and metric checks, spacing, nonuniform scaling, warm
  caches, rejected extreme sizes and recovery of subsequent font operations.
- Queue state checks, including old in-flight deliveries across a segment
  reset and valid out-of-order requests within the current segment.
- Actual XySubFilter DLL rendering at 1080p/4K/8K and custom 8K layout with 4K
  output, backward seeks, 128 retained frames, reopening and teardown.
- Actual VSFilter DLL rendering through its native CSRI adapter, including
  oversized text at 4K, backward seeks and reopening.
- Actual-DLL queue transitions with native and libass: repeated EOF/flush/new
  segment cycles, forward/backward seeks, same-thread and separate-thread
  refills, future packets, genuine late packets, timing changes and user options.

The test font is the unmodified OFL-licensed Kaushan Script 1.002 CFF font,
loaded privately. Its license and provenance are under `fixtures/fonts`.
The ASS fixture contains synthetic text. No sample video or dialogue is added.

## Build and validation

All three Release DLL targets and the console harness compile on Win32 and x64
with VS18/v145. A stale incremental-link debug-information failure required
clean filter/harness rebuilds; final builds succeeded with existing legacy SIMD
warnings. All six focused tests pass on both architectures.

Local binaries and detailed logs are in
`benchmark-results/font-queue-fix-20261102`.

### Pixel equivalence and failure controls

Both architectures pass 16 additional contracts covering prepared layouts,
lookahead, caches, geometry, reconstruction, karaoke keys, outline widening,
alpha evaluation, libass and actual-DLL layout settings. All 54 fixture outputs
match freshly generated 950 output exactly. For `brokensubs1.mkv`, all 536
frames match the archived 950 baseline in each of native default alpha,
native legacy alpha and libass modes (bitmap geometry and pixel hashes).

Running the new actual-DLL regressions against the original 950 binaries fails
as expected on both architectures: oversized CFF text disappears in XySubFilter
and VSFilter, and the EOF transition clears/refills the old queue with either
native or libass selected. These are assertion failures, not watchdog timeouts.
The candidate passes all four cases. See `validation.json` and per-case logs.

### Performance comparison

The full 2,428-frame credits cohort ran serially on CPU 7 in
950/candidate/candidate/950 order for each architecture. Every frame's bitmap
geometry and pixel hash matches between both builds and the retained 930
reference. The table reports the median of the two runs per build; times are
measured inside rendering, excluding output hashing.

| Architecture | Total render time, 950 to candidate | Change | p99 frame, 950 to candidate | Worst frame, 950 to candidate |
| --- | --- | --- | --- | --- |
| x64 | 21.121 to 21.693 s | +2.71% | 35.46 to 36.15 ms | 63.53 to 63.00 ms |
| Win32 | 20.603 to 20.931 s | +1.59% | 34.38 to 34.49 ms | 59.23 to 59.99 ms |

Thread CPU totals change by +3.55% on x64 and +0.76% on Win32. This small
two-run comparison shows modest timing differences, not zero overhead or a
statistically established speed change. Existing optimization contracts and
pixel equivalence pass; no large spike regression appears in this cohort.
The complete individual runs and measurements are in `performance.json`.

### Off-screen madVR validation

A freshly built Debug/LOCAL Zoom Player and x64 helper ran on undisplayed private
desktops with madVR 0.92.17.0, software decoding, a 128-frame queue, custom 8K
subtitle layout and a requested 4K video area. Diagnostic copies of the current
filter source logged actual requests, segment resets, clear dispatches and
bitmap counts. Completed traces establish that the candidate handled playback;
the test does not rely only on DLLs appearing in a module list.

- Native: two EOF loops of `broken subs 2.mkv`, then paused seeks to 0, 31, 89
  and 0 seconds. Each reset starts a new request horizon. There are no successful
  queue-clear callbacks after either loop. The first complete replay produces
  2,158 requests, compared with 8,201 requests and 148 clears during only the
  initial replay observation in the unfixed diagnostic run.
- Libass: one EOF loop of the same file and the same paused seeks, again with
  no stale horizon or successful queue-clear callbacks after the loop.
- Both backends issue exactly 128 requests when prefilling at zero after the
  final paused rewind. This is callback evidence of prefilling, not a captured
  madVR queue-overlay measurement.
- Native `broken subs 3.mkv`: the first requested frames after seeks to 1, 2,
  24, 35, 66, 86 and back to 2 seconds all contain visible subtitle bitmaps at
  custom 8K. Backward seeking retains the fix.

All three runs close successfully. The controller reports no desktop switching
or test windows taking focus; screenshots are disabled. The original player
executables/maps, player/filter settings, decoder selection and madVR queue
size are restored. The user's separately installed player remains untouched.

An initial live size check incorrectly appeared to show an empty native trace.
Reading the completed file after the helper exited recovered all 5,217 request
records and verified that run. Future checks read completed callback contents;
Windows directory metadata is not a reliable live trace-progress indicator.

These instrumented private-desktop runs validate playback transitions and
subtitle delivery. They do not certify visible-display FPS or physical 4K
presentation performance. Detailed summaries are in `madvr-validation.json`
and `private-player-validation.json` under the local results directory.

## Release 3.2.1.960 verification

All three DLLs and both benchmark consoles were rebuilt for Win32 and x64 with
the installed VS18/v145 toolchain. All six DLL file/product versions report
3.2.1.960; the actual-DLL property-page tests also verify the displayed version.

The versioned binaries pass 49 C++ contract runs per architecture: six font/queue
regressions, three outline regressions, and 40 other rendering, cache, alpha,
layout and actual-DLL contracts. All 11 Python benchmark-tool tests pass.
All 54 fixture outputs, 536 sample frames in each of native default alpha,
native legacy alpha and libass modes, and 50 streaming-console frames match
the 950 baseline exactly on both architectures.

The performance and off-screen madVR results above cover the same font and
queue implementation before the version-resource rebuild. The final release
validation record preserves that distinction and includes the earlier traces'
summaries, failure controls and timing results. No diagnostic logging is built
into the released filters.

The versioned binaries, matching PDBs, build logs, source hashes and validation
records are under `benchmark-results/release-v3.2.1.960`. Release packages include
the architecture-specific registration/unregistration helpers, with `/check`
verified before publication. Package manifests bind the assets to the release
commit; SHA256 checksums accompany the downloads.
