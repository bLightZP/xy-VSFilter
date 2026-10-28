# Native ASS top-three spike optimizations — 2026-10-27

Implemented the first three candidates from [the review](ass-spike-optimization-review-20261027.md) in the shared native renderer. Both architectures preserve exact pixels and bitmap geometry/order. The largest credit activations still exceed one source-frame period; these console results do **not** establish zero dropped frames in playback.

## What changed

1. **Constant drawing commands:** `CMD_p` and `CMD_pbo` can enter guarded layout preparation/replay because their existing evaluation is time independent, including inside transforms. Replay resets and verifies the final drawing mode/baseline. It preserves the existing first-closing-parenthesis parser, command order, step endpoints, alpha rounding, resets and stable intervals. Small polygon states retain the 4 KiB minimum charge under the existing 64 MiB reconstruction budget.
2. **Static vector work:** eligible future static polygons retain staged words and immutable vertices, with exact source/style/duration/effect/margins/PAR/screen checks before activation. Collision placement remains at activation. The 8,192-character preparation cutoff and existing budgets remain. A command-span polygon scanner replaces seven whole-string rewrites and temporary command strings while retaining legacy separators, `wcstod`, scaling and rounding. The activation improvement below combines staging and scanning; it is not an isolated scanner speedup.
3. **Unchanged draw state:** cached single-word lines can retain exact draw descriptors and successful dirty bounds. Guards include word/style identity, positions/origin/margins, clip, alpha, metrics/scales, subpixel level and output color context. Moving/fading/scrolling events, karaoke and inverse/vector clips fall back. Layer wrappers retain raster results weakly between requests; frame-scoped pins provide the normal strong-owner fast path only during composition. The last pin releases its raster owner at frame completion, so normal MRUs still govern lasting image ownership. Existing true-subpixel geometry keys are reused, with distinct quantized keys when required. Additive decisions are per-frame fields, never mutations of retained draw descriptors.

Draw metadata has a separate **32 MiB** maximum with a conservative **4 KiB per-plan charge** kept alive by outstanding draw references. Other generic image-cache limits were not enlarged. No condition recognizes the sample, a style name, a path literal or a timestamp. These changes serve both DirectVobSub and XySubFilter/consumer native paths; optional libass rendering bypasses them.

The first weak-only prototype was retained as `candidate-weak-only-*` / `abba-weak-only-*`. It reduced repeated setup but showed Win32 bitmap-stage regressions. The final implementation adds copy-safe, frame-scoped raster pins; the tables below measure the revised code against the same original baseline, with fresh passes. The rejected prototype is not pooled into final timings.

## Method and provenance

Frozen the current accepted G baseline before editing, verifying all 47 recorded source hashes and the DLL/console/PDB hashes for both architectures. Controls, candidate executables/PDBs, selected source snapshots and final DLLs are under `benchmark-results/top-three-20261027/`. Version remains **3.2.1.900**. Builds use installed VS 18 / v145 / SDK 10.0.26100.0, serial `/m:1 /nr:false`, logical CPU 7 at Normal priority. No software was installed.

Benchmarked the complete **2,428 requests**, frames 32128–34555, 22:20 through EOF, at 1920×1080 and 24000/1001 fps, using the already extracted default ASS track and private fonts. Input SHA-256: `1ece3acc51ef4b091a6af7ab9f87d989e314c15d95e25ed883283d47f9656d72`. Each architecture ran **ABBAABBA**, four fresh-process passes per build, on **one rendering thread**, logical **CPU 7 / affinity 0x80**, **Above Normal** priority, with 256 bitmap-cache entries. Builds and timings did not overlap. Full `RenderEx` includes synchronous bounded preparation; nested diagnostic phases overlap and must not be added as independent work. Thread CPU counters are coarse per request, so use pass totals.

Summary values are medians of four **pass-level** quantities. Spike-table values are **per-frame medians across four passes**, as in the review. They are new measurements against the frozen baseline, not comparisons to older saved wall times. No playback tests, player launches, COM registrations or player-setting changes were performed.

## Full interval

| Architecture | Thread CPU, baseline → new | CPU reduction | p99 request, baseline → new | Max request, baseline → new | Requests >41.708 ms, baseline → new |
| --- | ---: | ---: | ---: | ---: | ---: |
| x64 | 28.039 → 24.320 s | 13.3% | 44.05 → 39.41 ms | 88.61 → 71.34 ms | 31.5 → 20.5 |
| Win32 | 28.898 → 26.125 s | 9.6% | 45.74 → 41.16 ms | 83.73 → 79.04 ms | 35.5 → 23 |

## Review spike timestamps

| Frame / midpoint | x64 baseline → new | Change | Win32 baseline → new | Change |
| --- | ---: | ---: | ---: | ---: |
| 32249 / 22:25.073 | 57.84 → 19.85 ms | +65.7% | 82.24 → 24.45 ms | +70.3% |
| 32693 / 22:43.591 | 50.72 → 50.07 ms | +1.3% | 48.39 → 49.10 ms | -1.5% |
| 32780 / 22:47.220 | 64.17 → 63.29 ms | +1.4% | 66.94 → 65.65 ms | +1.9% |
| 32854 / 22:50.306 | 60.94 → 51.36 ms | +15.7% | 57.37 → 56.45 ms | +1.6% |
| 33157 / 23:02.944 | 78.48 → 60.45 ms | +23.0% | 63.95 → 61.75 ms | +3.4% |
| 33448 / 23:15.081 | 73.64 → 71.34 ms | +3.1% | 67.61 → 68.20 ms | -0.9% |
| 33973 / 23:36.978 | 51.91 → 52.43 ms | -1.0% | 54.84 → 56.81 ms | -3.6% |

Positive change means less time. Small positive/negative differences should be read against pass variance; this is not a claim that every activation improved. The 426-event vector spike is included explicitly even though it is outside the ≥1,000-event activation cohort.

| Architecture / frame | Parse, baseline → new | Draw setup, baseline → new | Bitmap stage, baseline → new | Descriptor hits / new tracked machines |
| --- | ---: | ---: | ---: | ---: |
| x64 / 32249 | 39.81 → 1.90 ms | 13.23 → 13.76 ms | 1.64 → 1.46 ms | 0 / 426 |
| x64 / 32780 | 12.65 → 12.41 ms | 22.56 → 22.81 ms | 16.35 → 17.13 ms | 3,309 / 3,163 |
| x64 / 33448 | 17.69 → 15.69 ms | 25.13 → 27.42 ms | 18.39 → 17.40 ms | 1,630 / 4,598 |
| Win32 / 32249 | 61.37 → 3.25 ms | 16.43 → 17.60 ms | 1.68 → 1.78 ms | 0 / 426 |
| Win32 / 32780 | 12.46 → 13.62 ms | 23.56 → 23.94 ms | 18.09 → 18.30 ms | 3,309 / 3,212 |
| Win32 / 33448 | 16.14 → 17.05 ms | 23.25 → 26.15 ms | 17.26 → 17.64 ms | 1,630 / 4,638 |

The bitmap stage includes group-key lookup, allocation and dispatch; it is not an isolated blend-kernel measurement. Newly activated or changed events still construct descriptors. Reuse chiefly helps subsequent unchanged requests, so activation cost can stay flat even when the surrounding burst improves.

| Following request | x64 baseline → new | Win32 baseline → new |
| --- | ---: | ---: |
| 32781 / +41.708 ms | 51.74 → 46.49 ms | 52.94 → 48.50 ms |
| 32782 / +83.417 ms | 52.12 → 48.25 ms | 55.34 → 55.54 ms |
| 32783 / +125.125 ms | 48.02 → 42.91 ms | 52.17 → 44.98 ms |
| 33449 / +41.708 ms | 43.47 → 45.56 ms | 40.81 → 46.06 ms |
| 33450 / +83.417 ms | 32.96 → 29.94 ms | 31.38 → 30.79 ms |
| 33451 / +125.125 ms | 28.29 → 24.30 ms | 27.44 → 23.93 ms |

## All 17 fixed activation bursts

Window totals below are medians of each pass’s sum of activation and the next two requests, at 0 / +41.708 / +83.417 ms, all within the following 120 ms. The first request after that boundary (+125.125 ms) is also retained in the detailed JSON. The cohort was selected from source event counts before examining these new timings.

| Frame / midpoint | x64 window baseline → new | Win32 window baseline → new |
| --- | ---: | ---: |
| 32693 / 22:43.591 | 110.73 → 103.78 ms | 103.97 → 104.52 ms |
| 32780 / 22:47.220 | 167.21 → 158.34 ms | 173.78 → 174.86 ms |
| 32854 / 22:50.306 | 148.35 → 131.67 ms | 161.37 → 143.45 ms |
| 32928 / 22:53.393 | 118.06 → 102.12 ms | 133.70 → 111.43 ms |
| 33082 / 22:59.816 | 100.65 → 96.71 ms | 103.83 → 93.76 ms |
| 33157 / 23:02.944 | 181.71 → 142.83 ms | 165.62 → 144.61 ms |
| 33254 / 23:06.990 | 60.96 → 54.85 ms | 57.46 → 56.60 ms |
| 33378 / 23:12.162 | 63.91 → 58.20 ms | 59.48 → 60.31 ms |
| 33448 / 23:15.081 | 150.07 → 147.14 ms | 139.32 → 144.84 ms |
| 33518 / 23:18.001 | 175.92 → 123.49 ms | 140.07 → 119.10 ms |
| 33586 / 23:20.837 | 85.14 → 78.56 ms | 80.45 → 76.40 ms |
| 33648 / 23:23.423 | 107.94 → 107.74 ms | 99.98 → 104.85 ms |
| 33741 / 23:27.302 | 69.65 → 73.45 ms | 69.45 → 70.04 ms |
| 33797 / 23:29.637 | 76.31 → 73.23 ms | 73.34 → 70.43 ms |
| 33898 / 23:33.850 | 85.16 → 83.94 ms | 79.19 → 87.86 ms |
| 33973 / 23:36.978 | 132.21 → 136.41 ms | 131.99 → 144.04 ms |
| 34048 / 23:40.106 | 119.85 → 105.06 ms | 105.46 → 128.16 ms |

## Reuse and memory

- **x64:** overlay queries 4,810,866 → 728,382 (84.9% fewer); descriptor hits 3,294,753, tracked machine factories 420,236, tracked draw factories 911,446, overlay-key factories 585,767. Peak charged draw metadata 25.72 MiB. Median peak process commit 439.3 → 457.4 MiB.
- **Win32:** overlay queries 4,810,873 → 719,712 (85.0% fewer); descriptor hits 3,299,889, tracked machine factories 426,506, tracked draw factories 924,823, overlay-key factories 593,676. Peak charged draw metadata 25.79 MiB. Median peak process commit 410.5 → 421.7 MiB.

Factory counters cover ordinary `boost::make_shared` calls in the tracked cached-event draw path; they supplement `xy_alloc_calls` and are not all CRT/ATL allocations or stack key constructions. Full first-pass preparation/layout/reconstruction counters and per-frame draw counts are in [the JSON](benchmarks/20261027-top-three.json). Layout admission remains 32 MiB, so some programs still fall back when that budget is full. These data measure the combined top-three change; they do not assign an independent speedup to each subchange.

The focused Win32 composition-only check at frames 33960–33985 compared the frozen weak-only prototype with frame-scoped pins in four passes each: median timed composition total 311.55 → 292.14 ms (+6.2% less). All 26 frame hashes/geometry match. This isolates composition: setup, pin acquisition/release and source reading are outside that phase; full-interval timings above include per-frame ownership work. Raw evidence is in `pin-proof-Win32/summary.json`.

## Packet-delivery model

Compared the same baseline and new code at 0 / 250 / 1,000 ms packet lead, four passes per build/lead/architecture over frames 32700–32830. Each timed request includes packet insertion/segment maintenance and rendering; setup/source reading is excluded. Lookahead is 4 ms and bitmap capacity 256 in both builds. Future undelivered events are hidden from the native renderer. This model does not measure actual splitter lead/provider cadence.

| Architecture / lead | Frame 32780 insertion + render, baseline → new |
| --- | ---: |
| x64 / 0 ms | 108.77 → 122.72 ms |
| x64 / 250 ms | 82.37 → 82.39 ms |
| x64 / 1000 ms | 59.95 → 57.47 ms |
| Win32 / 0 ms | 104.23 → 100.87 ms |
| Win32 / 250 ms | 80.52 → 79.50 ms |
| Win32 / 1000 ms | 57.96 → 54.38 ms |

The x64 zero-lead activation regresses here: **108.77 → 122.72 ms** at frame 32780. The 250 ms x64 case is effectively flat. These are recurring activation requests, not the excluded first model request or an accepted pause/resume drop. The combined change therefore improves overall CPU and several tails, but is not a universal spike reduction. Reducing work paid at activation when no advance data exists remains unresolved. Several late Win32 burst windows also regress, as the table above records.

All packet-model frame hashes/geometry match the full-script golden. The initial independently initialized request is reported separately in JSON; all later font/raster/activation spikes remain included. This is not a pause/resume test. The user accepts an isolated resume drop; recurring spikes remain the target.

## Exact-output gates and remaining work

- **32 focused contracts per architecture**, including constant/malformed/nested transforms and step boundaries; guarded static staging and invalidation; 2,000 polygon-scanner differentials against the frozen legacy scanner; draw reuse across five native bitmap formats/all subpixel levels; source edits, seeking, expiry, budgets, weak-owner eviction, retained frames, and the 128-item additive threshold.
- **54 original fixture cases** and **15 extended scripts × 2,428 frames** per architecture, including the new independent 160-event `prepared-draw-reuse.ass`, against accepted/frozen controls.
- Complete private sample at **256 and zero bitmap-cache entries**, diagnostic passes and isolated composition replay; all benchmark passes, cross-architecture output and packet-model requests match exact hashes plus bitmap geometry/order. **Zero mismatches**.
- Both actual filter Main-page/option factory checks per architecture, with no registry writes. All **six native DLL targets** and both console tools compile; matching DLL/PDB/source snapshots and final verification are saved locally.

Remaining over-budget credit activations need further work. The review’s grouping-topology/key reuse and dispatch/short-span composition candidates remain unimplemented here. Real packet cadence and end-to-end drops require a separately authorized Debug Zoom Player run; no playback was performed. The existing bitmap-key omission of pixel format is outside this patch; format-negotiation tests explicitly flush that MRU.

Reproduce the summary without rendering: `python docs/benchmarks/20261027-top-three-analysis.py`. Timing commands and full inputs/provenance are in the local ABBA manifests; [the benchmark README](../test/benchmark/README.md) describes the consoles. Detailed portable results: [JSON](benchmarks/20261027-top-three.json).
