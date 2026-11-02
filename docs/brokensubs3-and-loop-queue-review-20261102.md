# Blank native subtitles and the queue after looping

Reviewed 2026-11-02 against v3.2.1.950, commit
`9d266f7a606d58de8e66388b52f6f659bd446f56`. This is an investigation and proposed
implementation plan. Production renderer code, release files and installed DLLs
were not changed. Diagnostic programs and extracted sample material remain in
the ignored `benchmark-results` directory.

Implementation follow-up: [font and queue fixes](font-queue-fixes-20261102.md).

## 1. Confirmed: oversized GDI fonts make the new sample blank at custom 8K

`broken subs 3.mkv` contains a 90-second, 1920x1080 video, one ASS track with 66
events, and the attached `KaushanScript-Regular.otf` font. The script uses
`PlayResX: 1280`, `PlayResY: 720`; all three styles use Kaushan Script at size 50.
Its alpha override targets are `00` and `FF`. There is ordinary bottom dialogue
as well as animated text. The blank result does not require an alpha-policy
change or malformed subtitle content.

The native renderer scales the font to its intermediate GDI coordinate system
in [RTS.cpp](../src/subtitles/RTS.cpp), in `CreateSubtitle`:

```text
GDI font height = ASS font size * (layout height / script height) * 64
custom 8K:       50 * (4320 / 720) * 64 = 19200
custom 4K:       50 * (2160 / 720) * 64 =  9600
```

`CMyFont` passes that height to `CreateFontIndirect`. With this attached OpenType
CFF font, Windows returns a font handle at height 19200, but `GetTextMetricsW`
and `TextOutW` fail, and the recorded outline contains no points. At heights
4800, 9600, 15087 and 15360 the same private-memory font probe succeeds.
The font was loaded successfully; this is not a missing attachment.

Two related problems exist in `CMyFont`, `CText::GetTextInfo` and
`CText::CreatePath`:

- There is no bound or rescaling fallback for an oversized GDI font realization.
  Checking only whether `CreateFontIndirect` returned a handle misses this
  failure.
- `TEXTMETRIC tm` is uninitialized and the return value of `GetTextMetrics` is
  ignored. A failed measurement can therefore feed indeterminate metrics into
  native layout. The per-character spacing path also fails to check all outline
  operations.

Microsoft documents zero as failure for
[GetTextMetricsW](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gettextmetricsw)
and [TextOutW](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-textoutw).
The measured threshold is a property of this font/platform test, not an ASS
font-size limit or a documented universal GDI maximum.

### Reproduction and controls

Actual archived x64 XySubFilter DLLs were loaded directly, with an isolated test
configuration and the extracted font. At 2 seconds, with output size 3840x2160:

| DLL | Custom 1920x1080 layout | Custom 3840x2160 layout | Custom 7680x4320 layout |
| --- | --- | --- | --- |
| 3.2.1.920 | 2 visible bitmaps | 2 visible bitmaps | 0 bitmaps |
| 3.2.1.930 | 2 visible bitmaps | 2 visible bitmaps | 0 bitmaps |
| 3.2.1.950 | 2 visible bitmaps | 2 visible bitmaps | 0 bitmaps |

The 910 DLL does not support the layout-setting interface used by this probe;
that attempted run failed setup and is not evidence about its rendering.

Additional checks against the current shared renderer:

- At 1080p, a full 2,159-frame scan produced 1,742 nonempty frames in each backend,
  with no frame blank only in native. This checks visibility, not pixel equality.
- At custom 8K, all six selected subtitle-bearing timestamps were blank in native
  and visible in libass.
- A local script-only experiment changed font size 50 to 25 and ScaleX/ScaleY
  from 100 to 200. Native rendered all six 8K cases. This preserves the intended
  size approximately and demonstrates the font-realization cause; it is not a
  proposed sample edit or a claim of identical pixels.

[MPC-BE's current RTS.cpp](https://github.com/Aleksoid1978/MPC-BE/blob/master/src/Subtitles/RTS.cpp)
uses `MAXGDIFONTSIZE = 15087`. Above that size it reduces the GDI font size and
increases the word's X/Y scale proportionally. That is a concrete explanation
for why its ISR can avoid this failure. Libass uses its separate font/rendering
path and also avoids the failing native GDI realization.

This confirms the reported blank result under custom 8K. It does not establish
that this sample is blank at every resolution; the 1080p and 4K controls work.

### Proposed native fix and regression coverage

1. Normalize oversized native GDI font requests to a safe intermediate size and
   compensate both metrics and glyph geometry. Preserve authored size, spacing,
   alignment, wrapping, transforms and border geometry. Evaluate the normalization
   in the shared native path so VSFilter and XySubFilter receive the same fix.
2. Initialize metrics and check font realization/measurement/outline results.
   Do not cache failed or indeterminate measurements as valid layout. Merely
   zeroing the metrics would make failure deterministic but would not restore
   missing text.
3. Preserve existing cache keys, preparation/reconstruction reuse and immutable
   geometry ownership. Keep the normal-size path unchanged where possible. No
   libass or alpha-policy change is needed.
4. Add focused tests using redistributable CFF and TrueType fixtures: oversized
   fonts, custom 8K layout with 4K output, spacing, nonuniform scale, transforms,
   multiline layout, cold/warm caches, backward seeks and both native adapters.
   Check unchanged normal-size pixel output and the existing libass/alpha tests.

## 2. Queue after EOF and replay

The investigation uses `broken subs 2.mkv`, as suggested by the user. The queue
policy in [ConsumerInvalidation.h](../src/subpic/ConsumerInvalidation.h) and the
segment hook in [SubtitleInputPin2.cpp](../src/filters/transform/vsfilter/SubtitleInputPin2.cpp)
are unchanged between 930 and 950. Changes to `xy_sub_filter.cpp` in that interval
add the native alpha option, not a new loop policy. The 950 outline fix is not
itself evidence of a queue regression.

### Confirmed actual-DLL bookkeeping defect

A deterministic 128-slot consumer reproduces this sequence with archived 930
and 950 DLLs, using both native and libass:

1. Request the final 128 frames beginning at 85 seconds; the tracked request
   horizon reaches approximately 90.34 seconds.
2. Send EOF, flush and `NewSegment(0, ...)`. The segment hook schedules
   `Clear(0)`.
3. Have the consumer refill its old slots reentrantly during that clear. These
   old requests restore the end-of-file horizon.
4. Replace the consumer's slots with requests for the new 0-5.34-second interval.
5. Deliver a subtitle packet at 10 seconds. The filter wrongly notifies the
   consumer with `Clear(9.9999999s)`, despite that packet being beyond all current
   requested frames.

`Schedule()` resets the requested flag, but segment reset does not advance a
timeline generation. `Begin()` then retains the maximum requested subtitle stop;
requests from the start of the new timeline do not lower a horizon restored by
old requests. `Packet()` consequently classifies future packets as overlapping
previously requested output.

This test proves an unnecessary invalidation; its mock respects the partial
boundary, so that particular 10-second clear does not evict its 0-5.34-second
slots. The real madVR trace below establishes the corresponding playback
sequence and resulting repeated work.

The existing [queue contract](../test/benchmark/queue_contract.h) deliberately
supports reentrant refilling, but disables refilling before its `NewSegment`
check. It therefore misses this transition.

### Confirmed in madVR: old requests poison the new timeline

The final playback run used a freshly compiled Debug/LOCAL Zoom Player and its
matching x64 helper, madVR 0.92.17.0, software decoding, a 128-frame queue, custom
8K subtitle layout and a requested 4K video area. After the user's off-screen
instruction it ran on an undisplayed private desktop, with no foreground
activation or screen capture. The controller recorded 1,676 foreground checks,
no test window taking focus, and no desktop switch. All test players exited;
the original player executables, settings and queue size were restored.

A diagnostic DLL copied from the current source logs actual `RequestFrame`,
invalidation and clear-dispatch calls. Its trace proves this DLL handled the
active madVR requests. The first EOF/replay produces the following sequence
in `trace-85180.csv` (times converted from 100 ns units):

| Action | Request time | Tracked horizon | Effect |
| --- | --- | --- | --- |
| Last request before EOF | 89.965 s | 90.0067083 s | Expected old timeline |
| New segment invalidation | — | Requested flag reset | `Clear(0)` succeeds |
| Old request immediately after clear | 89.965 s | 90.0067083 s | Old horizon reinstated |
| New packet | 12.64 s | 90.0067083 s | Schedules an unnecessary partial clear |
| First request at replay start | 0 s | 90.0067083 s | Horizon does not move back |

The old refill in madVR occurs on its request thread after the clear returns;
it need not be a synchronous reentrant call. Every one of the 8,201 requests
logged after the loop retains that stale 90.0067083-second horizon. There are
148 successful clear dispatches after the loop, compared with three during
startup and the whole first pass. madVR repeatedly requests the same early
frames after these notifications. The run stops about 11 seconds into replay,
after an additional paused observation period.

This confirms the stale horizon / repeated invalidation mechanism with the real
consumer. It explains why queue refill competes with playback after looping,
and why pausing can eventually allow it to catch up. Earlier visible observation
showed the reported `0-128/128` queue range during replay and `128/128` after a
10-second pause. The off-screen trace diagnoses callback order; its disk logging
and undisplayed presentation make it unsuitable for an FPS comparison. It is
not certification of physical 4K display performance.

The original playback harness checked whether the requested DLL was loaded.
Both that DLL and the installed 940 DLL appeared in the process, so module
presence alone was insufficient to establish the active object. An initial
buffered trace was also empty after shutdown and is inconclusive. Explicitly
flushing the trace in the final run confirms current candidate execution; there
is no basis to claim that the earlier runs necessarily used the installed DLL.
The attempted full-player 930 control did not complete a useful replay test.
The direct-DLL reproduction establishes that the underlying bookkeeping defect
also exists in 930, but not that every 930 playback setup triggers it.

### Proposed queue fix and regression coverage

1. Give segment changes a dedicated timeline-reset operation, separate from
   user-setting changes. Reset requested ranges, pending invalidations and the
   generation of in-flight deliveries without automatically asking the consumer
   to refill its old EOF queue during its seek transition. Keep local subtitle
   data/provider invalidation. The leading implementation candidate is to stop
   issuing the unconditional consumer `Clear(0)` from `NewSegment`, relying on
   the graph's seek/flush transition for that queue reset; validate this with
   madVR and the existing consumer contracts before adopting it.
2. Ensure late work from the previous segment cannot rebuild the current request
   horizon or clear current deliveries. Advancing an epoch alone is insufficient
   if old requests can enter after that epoch change. Preserve correct partial
   clears for genuinely late packets, subtitle delay/rate conversion, user
   changes, connection changes and paused playback.
3. Extend the actual-DLL contract with EOF -> flush -> new segment, both immediate
   and asynchronous old-frame refills, backward/forward seeks, outstanding
   deliveries and repeated loops. A 10-second packet beyond a current
   0-5.34-second queue must not clear it; a packet overlapping that queue still
   must. Run native and libass, x86 and x64, and retain immutable-frame ownership
   and partial-boundary race checks. Avoid resetting the horizon on every lower
   timestamp: ordinary out-of-order refills must remain valid.
4. Inspect the VSFilter local-queue and bundled consumer paths as part of the
   change, without transplanting a madVR-specific callback policy into unrelated
   queues. Keep the preparation, outline, bitmap/cache and alpha optimizations.
5. Before release, run multiple EOF loops, seek near EOF, pause/refill and close
   with the actual candidate DLL and madVR. Strengthen the harness to verify the
   active graph object's version/module, rather than module presence alone.
   Continue off-screen; collect queue/callback evidence without taking focus.

Both issues now have actionable code-level causes. Implement and test the font
and queue changes separately, then run combined sample validation before another
release. No reporter follow-up is needed to begin these fixes. A report of
sample 3 remaining blank at ordinary 1080p/4K would require its exact settings,
because that variant was not reproduced here.

## Local evidence

- `benchmark-results/brokensubs3-20261102/`: extracted ASS/font, full visibility
  scan, six-frame comparisons, GDI font probe, direct-DLL matrix and local
  font-size proof.
- `benchmark-results/queue-loop-20261102/`: actual-DLL loop probe, source-isolated
  trace DLL, player build records, playback counters, lifecycle logs and settings
  restoration records.
- Sample 3 SHA-256:
  `ee17c757a85704cad68cc450bb59c221bec14df0a2065906c70252ad892b5087`.
- Attached font SHA-256:
  `c007ebe4c813374ed02da003394b73b39fa709ff5891c5613733c96be595ba28`.

The samples and extracted attachments are local investigation inputs and are not
included in the repository or a release.
