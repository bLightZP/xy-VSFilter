# `broken subs 2.mkv`: native outline failure

Date: 2026-11-01. Reviewed source: `bd2f27ac` (v3.2.1.940).

Follow-up: the requested production correction and its regression/validation
record are in [native outline fix](native-outline-fix-20261101.md). The investigation
and diagnostic-only results below describe the state before that implementation.

**A native renderer bug is confirmed.** The sample can overflow an outline arc's expiry value, then read outside an ellipse lookup table. The released x64 XySubFilter DLL reproduces the access violation with an 8K layout and 4K output. The archived v3.2.1.930 DLL fails at the same location. Libass succeeds for the same request in both releases.

This investigation leaves production source, release binaries and installed filter registration unchanged. Diagnostic harnesses and a trial correction are confined to ignored local artifacts. The exact unresponsive-player symptom, live seeking and playback-end behavior still need validation in the reporter's playback configuration; the tests below establish the renderer defect independently of that configuration.

## Sample and extraction

FFprobe reports a 90.007-second, 1920x1080 H.264 video at 24000/1001 fps, one default English ASS track, and the attached `gnuolane_free.ttf` font. The script uses a 1280x720 canvas and contains 5,743 dialogue events, including 459 vector-drawing events. Peak simultaneous activity is 79 events; at 31 seconds there are 49. It is an animated karaoke/effects script, rather than an exceptional event-count spike at exactly 31 seconds.

Extracted files are local:

- [ASS track](../benchmark-results/brokensubs2-20261101/track.ass)
- [Attached font](../benchmark-results/brokensubs2-20261101/fonts/gnuolane_free.ttf)
- [Container metadata](../benchmark-results/brokensubs2-20261101/probe.json)
- [Script statistics](../benchmark-results/brokensubs2-20261101/script-analysis.json)

Extraction used the existing FFmpeg installation, with subtitle stream copy and an explicit font destination. Fonts were loaded privately into test processes, not installed.

## Confirmed cause

The failing call chain is:

```text
XySubFilter::RequestFrame
  XySubRenderProviderWrapper2::Render
    CRenderedTextSubtitle::RenderEx
      CWord::PaintFromPathData
        ScanLineData2::CreateWidenedRegion
          WidenRegionCreaterImpl::xy_overlap_region
            WidenRegionCreaterImpl::add_line
```

The implementation is in [xy_widen_region.cpp](../src/subtitles/xy_widen_region.cpp):

1. `XyEllipse::cross_left` returns `MAX_CROSS_LINE` (`INT_MAX`) when there is no applicable intersection. It is a sentinel, not a row offset.
2. In `WidenRegionCreaterImpl::add_left`, the `i == 0` case adds that sentinel to the current row and assigns the result to `dead_line`. The corresponding `add_right` branch already checks `y < MAX_CROSS_LINE` before this addition; `add_left` lacks that check.
3. The addition overflows a signed integer. In the isolated 4K failure, `2147483647 + 1073744154` becomes `-1073739495` in the tested build. The coordinate values include the scan converter's `0x40000000` bias; the script is not requesting a billion-pixel image.
4. `add_line` retires an arc only when `cur_dst_line + 1 == dead_line`. A negative expiry is never reached by these positive rows, so the stale arc remains active. Assertions about expiry and nonempty queues do not protect Release builds.
5. The sweep subsequently uses invalid arc state and indexes outside `m_left_arc`, whose valid offsets are `[-ry, ry]`. The captured access violation is at the right-arc lookup called from line 391. That is where the bad state becomes a fault; the earlier left-arc sentinel arithmetic is the cause.

The relevant sentinel-handling and sweep code predates the recent optimizations. Local Git history traces it to the older widening implementation and its 2013 fixes. The reproduced v930 failure additionally establishes that the new v940 alpha setting did not introduce this defect. Turning that setting off in the native harness still crashes.

The same shared widening code is reachable from VSFilter through [SimpleSubPicProvider::RenderTo](../src/subpic/SimpleSubPicProviderImpl.cpp) and from XySubFilter through [XySubRenderProviderWrapper2](../src/subpic/XySubRenderProviderWrapper.cpp). The actual-DLL reproduction here covers XySubFilter x64; VSFilter and Win32 still need their own regression runs.

## Trigger and relationship to the report

The triggering effects draw thin, disconnected triangles with rotation, animated borders and blur. Around 30.34 seconds, two such drawing events become active. Changing the timestamp changes the animated border radius; changing output dimensions changes both the scan-converted shape and the widening radius. Consequently, one frame schedule can finish while a slightly different schedule encounters the defect.

The 4K crash was isolated to extracted ASS line 4171, active from 70.55 to 70.85 seconds, at timestamp **70.6330625 seconds**. It uses `\fr12`, `\fscx125\fscy125`, `\p1`, and borders animated through 6.25 and 2.5 script pixels. The failing radius is 82 subpixel units. Reducing its drawing to the following three triangles still reaches the same invalid expiry:

```text
m 24 -37 l 26 -37 l 28 -43
m 28 -43 l 30 -43 l 23 -42
m 23 -42 l 25 -42 l 17 -48
```

The [reduced ASS reproducer](../benchmark-results/brokensubs2-20261101/reduced.ass) retains the original timing/style/transform context. It requires no text glyphs to trigger the failure.

A finer timestamp sweep at an 8K raster catches the same invalid expiry at **30.4222291 seconds**, with radius 166. An 8K layout with 4K raster also encounters it at **16.9752917 seconds** under a frame-start schedule, and crashes at the 70.6330625-second request above. These are separate geometry/timing cases; they do not prove the reporter's exact 31-second playback position or rendering dimensions.

This explains why seeking can expose the problem inconsistently: it changes which point in an animated border is requested. A fresh process rendering only the failing event also crashes, so accumulated cache pressure is not required. Libass bypasses this native widening implementation, explaining the successful libass controls.

The corrupted sweep can continue with expired state before encountering inaccessible memory. XySubFilter renders under its filter lock, so excessive work there can block other filter operations. That is consistent with a player becoming unresponsive, but a live hang stack is still needed to attribute the reported seek/EOF freezes conclusively. An outline effect is intentional; an out-of-bounds read or unbounded sweep is a renderer bug.

## Reproduction evidence

Tests used bounded local console processes and matching symbols. They are correctness probes, not playback performance measurements. No player, madVR session, or Computer Use automation ran during this investigation.

| Test | Result |
| --- | --- |
| Native 1080p, complete script, 2,159 frame-midpoint requests | Completes and exits |
| Native 4K, complete script | Access violation at frame 1693, 70.6330625 s; reproduced twice |
| Native 4K, cold single-event request | Same access violation; no preceding playback needed |
| Libass 4K, complete script, 2,159 frame-midpoint requests | Completes and exits |
| Native 8K, complete frame-midpoint schedule | Completes; this schedule alone misses the defect |
| Diagnostic 8K sweep with a 4 ms timestamp offset | Detects invalid expiry at 30.4222291 s before allowing an invalid lookup |
| Diagnostic 8K layout / 4K raster, frame-start schedule | Detects invalid expiry at 16.9752917 s |
| Released v940 x64 DLL, 8K layout / 4K output, isolated failing event | Native access violation in `add_line`; libass delivers a bitmap and exits |
| Released v930 x64 DLL, same request | Same native access violation; libass succeeds |
| Native failing event with v940 alpha option disabled | Still fails |

The v940 DLL tested has SHA256 `fa4a0a5ba5c153c05fa0128f62111b792f707f375d6088202a37fd13e0b71283`. The DLL harness loads this archived release binary directly and isolates its registry settings; temporary test keys are removed, including after a crash.

An initial complete-script 8K libass probe was stopped by its 110-second wall-clock limit after 1,325 completed requests. It was still progressing; that result is incomplete, not evidence of a hang. The isolated libass controls above complete successfully.

### Diagnostic correction

A local copy of the widening implementation was linked into a separate probe executable. Its only behavioral correction makes the `add_left` sentinel check match `add_right`; additional checks stop on invalid arc state. This corrected probe:

- Logs the previously mishandled `INT_MAX` sentinel and completes the isolated failing request.
- Completes all 2,159 requests in the 4K midpoint run and the 8K-layout/4K-output frame-start run, including renderer teardown.
- Completes the previously failing 8K sweep near 30 seconds.
- Completes 160 alternating forward/backward/end-of-script requests while retaining up to 128 frame references, then exits.

This confirms the causal fix candidate. It is not a production patch or a complete pixel-equivalence/performance validation.

## Recommended implementation and validation

1. **Repair sentinel handling in the native widener.** Distinguish no-intersection results before doing row arithmetic, preserve the existing arc when it has no intersection, and audit the corresponding left/right branches for sentinel arithmetic. Keep the optimized widening algorithm for normal inputs. Fix the Debug assertions so they do not themselves evaluate overflowing expressions.
2. **Bound failure behavior.** Validate arc expiry, nonempty queues and ellipse indices before using them. On impossible state, return a controlled failure or use a bounded, verified fallback. Do not merely clamp the index or silently remove the outline: that hides the logic error and changes rendering. Review allocation/initialization failure propagation in the widening path at the same time.
3. **Add focused regressions.** Use the reduced three-triangle fixture and a direct span/arc case for `INT_MAX`, with both left/right paths, lookup-table and analytic-intersection radii, timestamp phases and 1080p/4K/8K geometry. The old code must fail the test and the corrected code must finish within a watchdog. Compare widened coverage with an independent bounded reference. Exercise cold requests, repeated/backward seeks, retained frames and teardown in both architectures and both filter paths. Preserve valid alpha behavior, the v940 alpha option, libass behavior, and existing optimization/output contracts.
4. **Validate actual playback before release.** Use the required Debug/LOCAL Zoom Player and helpers with madVR, the embedded track and attached font. Test the reporter's 8K layout / 4K output, 4K layout, repeated seeks around 30–32 s and the isolated 70.63 s trigger, then EOF/stop/close. Record queue depth, responsiveness and memory; capture stacks if any hang survives. Run matched native performance and pixel checks against the existing optimized code before publishing.

Until a corrected build is validated, **Prefer libass rendering** is the best-supported workaround for this sample. A lower resolution can avoid particular trigger geometries, but the timestamp sensitivity means it is not a reliable fix.

## Local evidence

All generated evidence is under [benchmark-results/brokensubs2-20261101](../benchmark-results/brokensubs2-20261101), ignored by Git. Key files:

- `trace-cold-4k.log`: symbolized native crash stack.
- `released-dll-native.log`, `released-dll-native-930.log`: actual release-DLL crash stacks.
- `released-dll-results.json`, `released-dll-results-930.json`: native/libass outcomes.
- `guard-cold-4k.log`, `phase8k-40000.log`: invalid expiry values before the unsafe lookup.
- `isolation.json`, `minimal.ass`, `reduced.ass`: event isolation and reduced geometry.
- `proof-minimal.log`, `proof-4k.json`, `proof-8klayout-4k-start.json`, `proof-8k-30s.json`, `proof-seeks-128.json`: bounded diagnostic-correction results.
- `libass-4k-full.json`: successful complete-script libass control.
- `analyze.py`, `run_case.py`, `run_dll.py`, `reduce.py`, `probe_dll.cpp`, and the local probe projects: reproducible investigation harnesses.
