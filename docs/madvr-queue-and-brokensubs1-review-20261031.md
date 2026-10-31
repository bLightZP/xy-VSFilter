# madVR queue regression and brokensubs1 review — 2026-10-31

Reviewed source: `cdf8bf6db046ad57f68ff5243b91aea3c7fd8e50`. Runtime comparisons use the archived, published x64 v3.2.1.910 and v3.2.1.920 binaries. Production code and the supplied MKV were not changed during this review.

## Conclusions

1. **v920 introduces a confirmed consumer-queue regression.** Its new full-clear policy is triggered by routine input changes, including every change in the consumer's reported frame duration. A test using the actual shipped DLL, a paused provider, and a mock consumer reproduced 128 full clears for 128 requests when the reported duration alternated by just one 100-nanosecond tick. v910 retained all 128 mock queue entries under the same conditions. Whether this exact trigger occurs in the user's madVR setup still requires a trace.
2. **The supplied sample's opacity flicker comes from the script's out-of-range animated alpha values.** All 40 transforms use `\alpha&F00&`, targeting decimal 3840. Both native and libass interpolate that value before narrowing to eight bits, producing repeated opacity wraps. Replacing only those targets with `\alpha&H00&` removes the reversals. Human author intent cannot be established: this could be a deliberate compatibility effect or an authoring mistake.
3. **“Still broken” does not yet identify which original symptom remains.** The released x64 DLLs still pass the focused layout/property-page tests. That establishes the tested behavior, not success in the user's playback setup. Obtain the exact remaining reproduction before changing unrelated fixes.

## 1. Confirmed queue regression

### Frame-duration changes now flush every queued frame

The path is:

1. `XySubFilter::RequestFrame` calls `UpdateParamFromConsumer` for each request.
2. That function reads `frameRate`, which this interface expresses as an integer frame duration in `REFERENCE_TIME` units. It computes `fps = 10000000.0 / rt_fps` and compares the result exactly with the stored double.
3. Any difference calls `XySetDouble(DOUBLE_FPS, fps)`.
4. `OnOptionChanged(DOUBLE_FPS)` calls `InvalidateSubtitle()`.
5. v920's `InvalidateSubtitle` schedules `ConsumerInvalidation`, whose worker calls `ISubRenderConsumer2::Clear(0)`.

Sources: [request](F:/Git/xySubFilter/src/filters/transform/vsfilter/xy_sub_filter.cpp:1196), [duration query/comparison](F:/Git/xySubFilter/src/filters/transform/vsfilter/xy_sub_filter.cpp:2040), [option invalidation](F:/Git/xySubFilter/src/filters/transform/vsfilter/xy_sub_filter.cpp:359), [unconditional scheduling](F:/Git/xySubFilter/src/filters/transform/vsfilter/xy_sub_filter.cpp:1940), [full-clear dispatch](F:/Git/xySubFilter/src/subpic/ConsumerInvalidation.h:38), and [interface contract](F:/Git/xySubFilter/include/SubRenderIntf.h:298).

An integer duration of 417083 versus 417084 ticks represents only 41.7083 versus 41.7084 ms. The precise duration of 24000/1001-fps video lies between them. The test deliberately alternates those values; it does **not** establish that madVR reports them that way. Genuine variable-frame-rate changes also need coverage.

### Reproduction using the published DLLs

The diagnostic probe loads each x64 DLL through its class factory, places it in a DirectShow graph with a mock consumer exposing `ISubRenderConsumer2`, and pauses the provider. HKCU is redirected to a disposable test subtree. After startup notifications settle, it requests 128 future frames, allowing the DLL's real worker to dispatch callbacks between requests.

The mock counts delivered request entries and discards them when `Clear(0)` arrives. No subtitle source is needed: null subtitle frames are valid deliveries under the interface contract. This isolates notification behavior from parsing, rendering cost, and GPU behavior.

| DLL | Reported frame duration | Requests / deliveries | Full clears | Mock queue entries remaining |
| --- | --- | ---: | ---: | ---: |
| v910 x64 | Constant 417083 ticks | 128 / 128 | 0 | 128 |
| v910 x64 | Alternating 417083/417084 | 128 / 128 | 0 | 128 |
| v920 x64 | Constant 417083 ticks | 128 / 128 | 0 | 128 |
| v920 x64 | Alternating 417083/417084 | 128 / 128 | **128** | **0** |

This is a reproducible regression in the shipped provider, with a queue model rather than madVR itself. It demonstrates a way queue prefill can fail while paused even without expensive subtitles. The reported `0–127/128` behavior is consistent with repeated clearing, but the user's actual trigger remains unobserved.

### Every selected embedded subtitle packet also schedules a full clear

[CSubtitleInputPin::Receive](F:/Git/xySubFilter/src/subtitles/SubtitleInputPin.cpp:619) calls `InvalidateSubtitle(tStart, stream)` after receiving each subtitle sample. [SubtitleInputPin2](F:/Git/xySubFilter/src/filters/transform/vsfilter/SubtitleInputPin2.cpp:65) forwards that to XySubFilter. For the active stream, v920 schedules a full consumer clear without checking whether the packet affects any requested or delivered frame.

Thus a packet beginning beyond the consumer's queued interval still throws away valid earlier output. The original timestamp is retained for local-provider invalidation but discarded by the consumer-notification helper. This is especially costly with densely packetized ASS effects. It is separate from the duration-query trigger above and affects both rendering backends.

The four packets in `brokensubs1.mkv` alone cannot establish an endless paused clear loop: after those packets stop and consumer options stabilize, this trigger stops. Log the invalidation reason to distinguish packet-driven clearing from query-driven clearing in the user's case.

### Why the previous tests missed this

The existing `ClearConsumer` test checks coalescing, reentrant invalidation, and a late-delivery race, but it does not connect the actual filter to a request-generating consumer. It explicitly requires a zero clear boundary. Full-script pixel comparisons bypass consumer delivery entirely. These checks passed while the invalidation policy remained wrong.

See [mock](F:/Git/xySubFilter/test/benchmark/layout_contract.h:59) and [helper tests](F:/Git/xySubFilter/test/benchmark/layout_contract.h:254). The new diagnostic covers the missing integration boundary and should become a regression test when the fix is implemented.

## 2. Flicker in the supplied sample

### Extracted content

`brokensubs1.mkv` has a 90.007-second, 1920×1080, 24000/1001-fps video, one default ASS subtitle track, and 13 font attachments. It has four active dialogue packets, all using style `OP 2` / font `Artois`. Other event records in the extracted script are comments.

| Dialogue | Original MKV start–end | Animated alpha targets |
| --- | --- | ---: |
| 1 | 1.656–6.506 s | 13 |
| 2 | 6.506–10.936 s | 6 |
| 3 | 10.936–14.546 s | 10 |
| 4 | 14.546–21.226 s | 11 |

FFmpeg's standalone ASS export rounds these boundaries to centiseconds, adding 4 ms here. Original packet times and payloads are preserved in `packets.json`; all standalone renderer comparisons use the same exported timestamps.

Sample SHA256: `c04ef71ba4d7ca99a5e0c52e70a920e4b05b82dc76b4394626d9de9fb136eeff`.

### Mechanism

The script repeatedly combines karaoke sweep tags with transforms such as:

```ass
\t(4425,6120,\alpha&F00&\1c&HFFFFFF&)
```

The documented ASS alpha range runs from `00` (opaque) to `FF` (transparent). `F00` is 3840, or 15 × 256. See the [Aegisub alpha reference](https://aegisub.org/docs/latest/ass_tags/#set-alpha).

The native parser strips the surrounding `&`/`H` characters, parses the remaining value in base 16, interpolates toward the full value, and only then casts the result to `BYTE`. Libass likewise parses the full alpha, interpolates it, and then converts the result to `uint8_t`. In the tested binaries, the effective alpha behaves as:

```text
truncate(start_alpha + progress * (3840 - start_alpha)) & 255
```

As the transform advances, alpha repeatedly crosses an 8-bit boundary, making the affected glyphs cycle between more and less transparent. `\fad(250,250)` and `\kf` also produce ordinary fade/karaoke changes, but they do not explain these repeated wraps within a syllable's transform.

Native evidence: [tag normalization](F:/Git/xySubFilter/src/subtitles/RTS.cpp:2909), [numeric conversion](F:/Git/xySubFilter/src/subtitles/RTS.cpp:2951), [alpha assignment](F:/Git/xySubFilter/src/subtitles/RTS.cpp:3066), [interpolation](F:/Git/xySubFilter/src/subtitles/RTS.cpp:3822). Vendored libass evidence: [alpha assignment](F:/Git/xySubFilter/src/thirdparty/libass-deps/sources/libass/libass/ass_parse.c:157), [parsing](F:/Git/xySubFilter/src/thirdparty/libass-deps/sources/libass/libass/ass_parse.c:231), [transform application](F:/Git/xySubFilter/src/thirdparty/libass-deps/sources/libass/libass/ass_parse.c:567).

### Controlled checks

- Rendered frame midpoints 0–535, covering every dialogue, with the extracted fonts. **Native v910 versus native v920: 536 matching frames. Libass v910 versus libass v920: 536 matching frames.** These are within-backend comparisons; the two different renderers are not expected to have identical pixels.
- Changed only `\alpha&F00&` to canonical `\alpha&HF00&`. Both backends still produced **536/536 identical frames** compared with the original. The omitted `H` is not the cause.
- Changed only the 40 targets to `\alpha&H00&`. Measured per-frame opacity in a 60-pixel-wide region covering part of the final word, between 19.040 and 20.625 seconds. The original has **25 decreases and 13 increases** over 38 frame transitions in each backend. The in-range variant has **zero decreases and 38 increases** in each backend.
- Independently rendered a 2.3-second comparison with FFmpeg's libass (`0.17.4-27-g1cbba0a8818c83495114b3c2b5730410eecbdb2c`), using the extracted Artois font. This path bypasses xySubFilter and madVR completely and shows the same script-driven behavior.

The correct conclusion is **script-driven flicker under the existing compatibility behavior**, not proof that the author meant to flash the text. A renderer-wide clamp or masking before interpolation would change the appearance of this and other scripts. Do not make that change as a flicker fix without an explicit compatibility decision.

The `alpha-00.ass` file is a diagnostic alternative assuming an opaque transform target was intended; it is not a correction silently applied to the supplied media.

## 3. Status of the original layout report

The three shipped-x64 layout contracts were rerun successfully against the archived v920 DLLs. They cover both actual More property pages, staged typing, invalid Apply, successful Apply, cancel, API/registry validation, and the native wrapper's layout/output distinction.

The relevant code remains [staged validation](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSubPropPage.cpp:356), [More-page instant-update disabling](F:/Git/xySubFilter/src/filters/transform/vsfilter/DirectVobSubPropPage.cpp:1974), and [actual original-video selection](F:/Git/xySubFilter/src/subpic/XySubRenderProviderWrapper.cpp:318).

Consequently, the tests support those specific fixes, but do not refute the user's observation. Ask whether item 1 means the typing lock, the original-size performance problem, or flickering. Capture the **loaded DLL path and file version**, player/graph architecture, exact More-page state, actual output/target dimensions, and reproduction steps. The download archive name alone does not establish which filter instance the player loaded. The confirmed queue regression can also obscure the benefit of an otherwise correct layout change.

## Next steps

1. **Prioritize a queue hotfix.** Separate local rendering-cache invalidation from external queue invalidation. Consumer option sampling during `RequestFrame`, including small frame-duration changes, must not automatically trigger `Clear(0)`. Keep per-request rendering inputs correct, including real VFR changes, without invalidating valid previously requested frames merely because the next request has a different duration.
2. **Make packet invalidation selective.** Track the requested/delivered interval for the current stream/connection generation. A packet beginning beyond that interval should not clear the consumer. For a genuinely late packet, preserve the earliest affected boundary and use a coalesced partial clear where supported. Convert subtitle time back to consumer/video time using the active delay/speed mapping; test equality/overlap at the interval boundary. Keep full clearing for genuine global changes such as track, layout, style, and timing changes.
3. **Preserve lock and lifetime safety while narrowing notifications.** Keep external callbacks outside filter/provider locks. Scope generations and pending notifications to the connected consumer/stream. Cover delivery racing a clear and avoid repeated whole-queue clearing as a response to every older generation. Record render failures separately so a failed request is distinguishable from a queue purge.
4. **Expand the release gate.** Promote the actual-DLL constant/alternating-duration probe to a regression test, add a bounded consumer that refills after Clear, and inject ahead-of-queue and late embedded packets. Cover 128-frame prefill while paused, normal playback, true VFR, seek, track/settings changes, delay/speed mapping, consumer replacement, legacy consumers, and render errors. Run both architectures and both backends. Retain the existing pixel, memory, and ownership checks.
5. **Validate in real madVR before another release.** Use the required Debug/LOCAL Zoom Player build, verify the loaded DLL, and compare v910 with the candidate. Record queue depth while paused and playing, callback counts/reasons, reported frame durations, request/delivery timestamps, HRESULTs, and effective geometry. Use both this small sample and the established heavy credits workload. This distinguishes queue-policy repairs from rendering performance.
6. **Resolve the sample report with the author/user.** Supply the extracted ASS and the original-versus-in-range comparison. If smooth opacity was intended, correct the subtitle targets after that intent is confirmed. Preserve the current renderer interpretation and add an out-of-range-alpha compatibility fixture. Continue investigating only any additional flicker that remains with valid in-range alpha.

## Local evidence

All extraction and diagnostic artifacts are under `benchmark-results/brokensubs1-20261031`. No media or fonts were uploaded, and no production DLL was installed or registered.

- [Extracted subtitle track](F:/Git/xySubFilter/benchmark-results/brokensubs1-20261031/track.ass), [in-range diagnostic variant](F:/Git/xySubFilter/benchmark-results/brokensubs1-20261031/alpha-00.ass), and [original packet data](F:/Git/xySubFilter/benchmark-results/brokensubs1-20261031/packets.json).
- [Comparison video](F:/Git/xySubFilter/benchmark-results/brokensubs1-20261031/alpha-comparison.mp4): source time approximately 19–21.3 seconds; original alpha above, in-range alpha below. Both panels use independent FFmpeg/libass rendering.
- [Queue probe results](F:/Git/xySubFilter/benchmark-results/brokensubs1-20261031/queue-probe.json), [actual-DLL probe source](F:/Git/xySubFilter/benchmark-results/brokensubs1-20261031/queue_probe.h), and [pixel probe source](F:/Git/xySubFilter/benchmark-results/brokensubs1-20261031/sample_probe.cpp).
- [Version comparison](F:/Git/xySubFilter/benchmark-results/brokensubs1-20261031/version-comparison.json), [opacity measurements](F:/Git/xySubFilter/benchmark-results/brokensubs1-20261031/opacity-comparison.json), [shipped layout tests](F:/Git/xySubFilter/benchmark-results/brokensubs1-20261031/shipped-layout-x64.json), and [evidence manifest](F:/Git/xySubFilter/benchmark-results/brokensubs1-20261031/review-evidence.json).
