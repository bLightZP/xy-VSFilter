# Consumer queue fixes and validation — 2026-10-31

This implements the queue findings in [the v920/sample review](madvr-queue-and-brokensubs1-review-20261031.md). Initial implementation testing used binaries reporting **3.2.1.920**. These fixes are included in [v3.2.1.930](releases/v3.2.1.930.md); its separate release rebuild is recorded below.

## Changes

- **Consumer parameters are request inputs.** `UpdateParamFromConsumer` updates FPS, geometry and color context locally. It no longer routes a queried frame duration through the user-option path that scheduled `Clear(0)`. True VFR changes remain effective; durations are not rounded to hide jitter. Zero/unavailable frame durations use the existing fallback without dividing by zero.
- **Embedded packets clear only affected queued output.** Local provider invalidation still happens for every selected-stream packet. A packet at or beyond the requested subtitle interval's exclusive end does not notify the consumer. Earlier packets coalesce to the earliest affected video-time boundary. Full clears remain for global user changes and selected-stream segment resets.
- **Timing and races retain their boundaries.** Delivery returns the original requested video timestamps. Partial-clear boundaries invert subtitle delay/speed conservatively, include equality, and fall back to a full clear for unrepresentable/non-forward mappings. In-flight deliveries record only invalidations affecting their interval; completion can reschedule that partial boundary. Failed deliveries and old consumer epochs cannot restart a clear against a replacement connection.
- **Callbacks preserve ownership.** The worker calls optional `ISubRenderConsumer2::Clear` outside the filter/provider and queue-state locks. Each request retains the consumer it rendered for. Existing immutable frame storage and the legacy consumer fallback remain in place.
- **Seeks reset the horizon.** `SubtitleInputPin2::NewSegment` invalidates the selected provider and clears the old requested horizon, so a packet following a backward seek is not classified using the previous segment's lookahead.

The native parser, libass, composition, alpha arithmetic, bitmap admission limits and existing rendering caches are unchanged. The separate VSFilter subpicture-queue path and bundled synchronous consumer were inspected; neither uses the new optional consumer-clear policy.

Primary files: [filter](../src/filters/transform/vsfilter/xy_sub_filter.cpp), [queue state](../src/subpic/ConsumerInvalidation.h), [segment hook](../src/filters/transform/vsfilter/SubtitleInputPin2.cpp), [regression contracts](../test/benchmark/queue_contract.h).

## Regression results

Built the three DLL targets and both console harnesses in Release with the installed VS v145 toolset, for Win32 and x64. Each architecture passed **41 contracts**: 34 existing renderer contracts, three layout/page contracts, and four new queue/alpha contracts. The 11 existing Python tests also passed.

The actual-DLL queue test connects a 128-slot consumer to XySubFilter and sends ASS packets through its real DirectShow input pin. Both native and libass paths pass:

- 128-frame paused-style prefill with constant durations, alternating `417083/417084`, true VFR changes, changing target geometry and zero-duration fallback.
- Future packets and exclusive-end equality without consumer clears; genuinely late packets with partial invalidation and synchronous consumer refill.
- Delay/speed mapping, original video delivery timestamps, global layout changes, segment resets, legacy consumers, invalid geometry and failed delivery propagation.
- Separate queue-state checks for coalescing, affected/unaffected in-flight delivery, failure, connection epochs, fractional boundaries and overflow fallback.

The archived v920 DLL fails the new integration test in **both backends**: the jitter phase causes **128 full clears**, versus **zero** with the candidate. The bounded negative control exits with an assertion failure, without requiring the watchdog. Its consumer stops honoring additional clears after eight to bound a broken refill path; its remaining-slot count is therefore not a model of madVR's exact depth.

| Pixel comparison against v920 | Win32 | x64 |
| --- | ---: | ---: |
| Existing numeric/ASS fixture frames | 54 identical | 54 identical |
| Heavy credits frames | 2,428 identical | 2,428 identical |
| Zero-lead embedded-packet smoke frames | 50 identical | 50 identical |
| `brokensubs1` native frames | 536 identical | 536 identical |
| `brokensubs1` libass frames | 536 identical | 536 identical |

The new alpha contract additionally checks 40 frame comparisons across both backends and both `&F00&`/`&HF00&` spellings. It requires interpolation toward the full target followed by eight-bit narrowing, matching the existing script-driven flicker behavior.

The 2,428-frame native console run took 21.99 seconds on Win32 and 22.09 seconds on x64, versus 22.29/23.54 seconds in the archived v920 measurements. Candidate p99 times were 34.20/37.15 ms. These are sanity checks against historical captures, not controlled evidence of a new rendering speedup. No render optimization was removed.

## madVR validation

Playback uses newly rebuilt **Debug/LOCAL** Zoom Player and helpers. The benchmark verifies their build-record hashes, the selected helper architecture, loaded candidate DLL path/hash and actual madVR renderer. Player/COM registration overrides are restored. Explicit backend, decoder and 128-slot madVR overrides are also backed up and restored.

The normal configuration uses D3D11 video decoding and a 16-slot madVR queue. The supplied sample completed after a 60-second paused hold, with one dropped frame at resume and no subsequent increase. The heavy credits run completed with **zero additional dropped frames** during measured playback; six drops were already present before the paused measurement. Captures show **16/16 paused** and **15–16/16 playing**.

The candidate fills **128/128 while paused** with software video decoding and sustains **125–128/128** through the heavy credits, completing playback with **zero dropped frames**. The matching v910 control also completes with zero drops. The x64 libass sample run sustains **127–128/128** and completes with one drop at resume and no subsequent increase.

| Completed run | Decoder / queue capacity | Observed subtitle queue | Additional dropped frames |
| --- | --- | --- | ---: |
| Candidate x64, heavy credits, native | D3D11 / 16 | 16/16 paused; 15–16/16 playing | 0 |
| Candidate x64, heavy credits, native | Software / 128 | 128/128 paused; 125–128/128 playing | 0 |
| v910 x64 control, heavy credits, native | Software / 128 | 127–128/128 playing | 0 |
| Candidate x64, supplied sample, native | D3D11 / 16 | TCP completion recorded; depth not captured | 1 at resume |
| Candidate x64, supplied sample, libass | Software / 128 | 127–128/128 playing | 1 at resume |
| Candidate Win32, supplied sample, native | Software / 128 | 128/128 paused; 127–128/128 playing | 0 |

All six runs reached EOF. Both installed madVR binaries report file version `0.92.17.0`; exact module hashes are retained with each capture. The statistics overlay was enabled for captures. Queue capacity, backend, decoder, player routing and COM registration overrides were restored.

Queue depths above come from captured madVR overlays, not inferred from TCP frame counters. The capture script identifies the benchmark's own `TMainForm` by its recorded PID and captures that desktop rectangle; window-only GDI capture omitted the accelerated video surface. The 128-slot candidate captures also record the contemporaneous player state and TCP statistics.

### Separate 128-slot hardware-decoder limitation

With the local D3D11 decoding configuration and madVR's CPU/subtitle queue set to 128, a seek stalls with **both decoder and subtitle queues empty**. This occurs with the candidate **and archived v910**. With software decoding, the v910 control fills **127–128/128** during playback and completes the credits with zero dropped frames. This isolates an additional hardware-decoding/graph interaction; it does not establish its exact root cause or attribute it to the new queue policy.

The benchmark now treats playback stopping before EOF as a failure. These stalled runs are retained as failures, not counted as successful queue validation. Before attributing a similar user report to XySubFilter, compare decoder mode, the decoder queue and the subtitle queue together.

## Reproduction and evidence

See [benchmark instructions](../test/benchmark/README.md#consumer-queue-regression-tests) for the supported test commands. Generated binaries, JSON results, CSV hashes, Debug build records and madVR captures are under `benchmark-results/queue-fixes-20261031` (ignored by Git). The supplied media and extracted fonts remain local.

- `validation.json`: both architecture results and binary hashes.
- `playback-validation.json`: successful and failed playback runs, counters and restoration records.
- `queue-v920-final-negative-control.json`: shipped-DLL failure detected by the new tests.
- `Win32/` and `x64/`: tested binaries, all contract logs and exact pixel comparisons.
- `madvr-credits-native-x64/`: 16-slot D3D11 credits run and queue captures.
- `madvr-credits-128-v910-x64/` and `madvr-credits-128-native-x64/`: stalled D3D11 controls.
- `madvr-credits-128-v910-software-x64/`: successful software-decoded v910 control.
- `madvr-credits-128-candidate-software-x64/`: candidate 128-slot validation.
- `madvr-sample-128-libass-x64/`: supplied-sample libass playback with a 128-slot queue.
- `madvr-sample-128-native-Win32/`: supplied-sample native playback in the Debug Win32 helper.
- `*-settings.json` and each `filter-test-setup.json`: restoration records.

## Release decision

The candidate passes the queue regression gate and real madVR playback checks above. Keep the 128-slot D3D11 seek limitation distinct from the repaired queue-clear loop: it also reproduces with v910 and still needs a decoder/renderer-specific investigation if that configuration must be supported. Obtain the reporting user's exact decoder mode and madVR build before claiming their entire playback configuration is resolved. The implementation was committed separately as `c3364224`.

## v3.2.1.930 release validation

All three DLLs and both benchmark consoles were rebuilt for Win32 and x64. All six DLL file/product versions are **3.2.1.930**. The release binaries passed **82 contracts** and the **11 Python tests**. Per architecture, 54 fixture outputs, 2,428 credits frames, 50 streaming frames and 536 sample frames per backend matched the v920 references. Cross-architecture credits output also matched.

The versioned x64 XySubFilter DLL completed the heavy credits in Debug/LOCAL Zoom Player with madVR 0.92.17.0, native subtitles, software video decoding and a 128-slot queue. After a 30-second paused hold, it reached EOF in approximately 102 seconds with **zero dropped frames**. The loaded DLL path/hash was verified and temporary settings were restored.

Both packages include `RegisterFilters.cmd`, `UnregisterFilters.cmd` and their shared helper. Fourteen checks cover architecture selection from native/WOW64 command processors, paths with spaces and shell metacharacters, missing DLLs, and mocked administrator-elevation requests/cancellation. Generated release evidence is under `benchmark-results/release-v3.2.1.930`; release assets include validation results, binary hashes, matching symbols and checksums.
