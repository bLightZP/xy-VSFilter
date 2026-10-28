# Cold ASS tag/parameter storage — 2026-10-28

Optimized storage and heap traffic in the shared native renderer, using the preceding validated [scalar-reuse build](performance-20261027-zero-lead.md) as the control. This is a generalized representation change: no filename, style, timestamp, drawing literal or expected event count selects it. The CRT numeric conversions and legacy parser grammar are unchanged. No playback tests were run.

## Implementation

- **Lazy packed tag lists:** an empty nested program is one pointer. The first allocation holds list bookkeeping and eight stable nodes; later blocks hold sixteen. A node carries one forward link, with no per-node backward link. Iteration and first-argument reads are header-visible and explicitly inlined. Published programs remain immutable.
- **Compact owned parameters:** first CString, count and spill pointer stay in the tag. Multi-argument storage combines its capacity header and raw slots in one allocation. Only live strings are constructed. First spill reserves three slots; larger lists grow geometrically. Aliased values survive growth, and truncate/refill releases the correct owners.
- **Ownership and accounting:** outer program/control ownership uses `make_shared`; destructors release only constructed nodes/strings. Source strings remain owned, and retained frames/programs do not borrow scratch buffers. Numeric spill arrays retain their existing representation. Admission charges actual node/spill/string capacity plus conservative owner/allocator bookkeeping. Syntax/reconstruction/layout limits remain 64/64/32 MiB, with existing raster policies. Smaller accounting can admit more syntax in other workloads; process commit is reported separately.

| Architecture | Node bytes, old → new (including links) | Parameter header bytes | Common spill growths, old → new |
| --- | ---: | ---: | ---: |
| x64 | 128 → 64 | 40 → 24 | 20 → 9 |
| Win32 | 72 → 40 | 20 → 12 | 20 → 9 |

Header/node sizes exclude owned string buffers and numeric arrays. Spill counts cover one command of each arity 2–7 in the focused contract; they are not all CRT allocations or a performance multiplier.

## Method

Before editing, verified **51 recorded source hashes and 10 executable/DLL/PDB hashes per architecture** against the last validated binaries. Frozen controls and candidate/stable source/PDB snapshots are under `benchmark-results/tag-storage-20261028/`. Final children verify **CPU 5 / 0x20, Above Normal, one render thread**. Builds use installed VS18/v145/SDK 10.0.26100.0, serial `/m:1 /nr:false` at Normal priority. No build overlaps timing and no software is installed. Version stays **3.2.1.900**.

Full render covers frames **32128–34555**, 22:20 through EOF: **2,428 requests**, 1920×1080, 24000/1001 fps, bitmap capacity 256. ABBAABBA gives four fresh processes per build. Timed `RenderEx` includes bounded synchronous preparation; diagnostic phases overlap and must not be summed as independent work. Isolated parse/reconstruction uses two passes per build and has different cache history; it is not an isolated tag-allocation measurement and does not produce rendered pixel proof. Thread CPU has coarse per-request resolution; totals are medians of pass totals.

Packet comparisons use six passes per build at zero/250 ms lead and four at 1,000 ms on Win32; x64 adds four confirmation passes at 1,000 ms after an initial adverse result, with all eight retained, alternating AB/BA. Each covers 131 chronological insertion-plus-render requests, frames 32700–32830. Input/header/style loading is excluded; unseen future events are withheld. Lookahead is 4 ms. Only the first independently initialized request is separated; recurring activation at **32780 / 22:47.220** remains included. This is a delivery model, not actual DirectShow cadence or measured drops. Architecture time blocks have different background load; use their fresh controls, not cross-block absolute times. Input SHA-256: `1ece3acc51ef4b091a6af7ab9f87d989e314c15d95e25ed883283d47f9656d72`.

## Full x64 interval

| Scope | Control CPU | Candidate CPU | Reduction |
| --- | ---: | ---: | ---: |
| Full RenderEx | 24.945 s | 24.789 s | +0.6% |
| Isolated parse/reconstruction | 3.117 s | 3.039 s | +2.5% |

Median per-pass p99 full request: **41.74 → 40.76 ms**.
Median per-pass maximum full request: **75.98 → 75.06 ms**.

| x64 memory | Control → candidate |
| --- | ---: |
| Peak process commit | 448.57 → 441.00 MiB |
| Peak charged syntax | 63.29 → 47.40 MiB |

Per-pass counts, raw samples, all nested timing metrics and build/source hashes are in [the portable JSON](benchmarks/20261028-tag-storage.json). Accounted syntax is an admission bound, not process RSS.

## Review spike timestamps, x64 full render

| Frame / midpoint | Control → candidate |
| --- | ---: |
| 32249 / 22:25.073 | 19.68 → 19.60 ms |
| 32693 / 22:43.591 | 49.04 → 50.05 ms |
| 32780 / 22:47.220 | 67.89 → 64.48 ms |
| 32854 / 22:50.306 | 53.45 → 53.29 ms |
| 33157 / 23:02.944 | 64.55 → 70.74 ms |
| 33448 / 23:15.081 | 71.93 → 73.01 ms |
| 33973 / 23:36.978 | 54.44 → 54.13 ms |

The 23:02.944 spike regresses **64.55 → 70.74 ms**. Its parse median is 15.07 → 15.00 ms, draw setup 23.51 → 22.50 ms and composition 24.88 → 28.38 ms; independently computed medians must not be summed. This remains an adverse total result even though no composition code changed. Full-render CPU is approximately flat, and the modest timing differences should be read against the retained pass samples.


## Packet activation and following requests

| Architecture / lead | Activation | Activation + next 3 requests | Interval CPU |
| --- | ---: | ---: | ---: |
| x64 / 0 ms | 109.21 → 106.05 ms | 248.25 → 247.89 ms | 2.734 → 2.719 s |
| x64 / 250 ms | 89.40 → 88.31 ms | 226.07 → 225.51 ms | 2.672 → 2.695 s |
| x64 / 1000 ms | 63.38 → 63.69 ms | 197.28 → 203.13 ms | 2.820 → 2.781 s |
| Win32 / 0 ms | 108.03 → 106.54 ms | 241.99 → 243.69 ms | 2.648 → 2.633 s |
| Win32 / 250 ms | 93.68 → 83.79 ms | 225.41 → 216.72 ms | 2.703 → 2.633 s |
| Win32 / 1000 ms | 59.29 → 59.95 ms | 190.15 → 193.56 ms | 2.672 → 2.672 s |

The x64 1,000 ms four-request window is about **3.0% slower**; Win32 zero/1,000 ms windows also regress slightly. Faster cold activation does not imply every following request is faster.


Zero-lead raw activation samples (ms; all passes retained):

- **x64:** control 116.27, 108.97, 109.32, 111.62, 106.76, 109.09; candidate 103.04, 115.81, 107.48, 103.39, 105.23, 106.87.
- **Win32:** control 123.16, 105.35, 109.94, 107.32, 108.73, 104.72; candidate 135.05, 101.76, 112.64, 108.21, 104.86, 103.23.

## Exact-output and build gates

- **34 focused contracts per architecture:** the added storage contract checks 6,000 parameter operations, reserve/truncate/refill/alias ownership, 5,000 stable tags, nested programs, separate numeric spills and retained shared owners. Existing locale/rounding/Win32 precision, legacy syntax, styles, layouts, geometry, retained-frame and blending contracts pass.
- **54 original fixtures and 17 independent scripts × 2,428 frames per architecture**, including the new 100-event `tag-storage.ass`. Full private sample at 256 and zero bitmap entries; diagnostic/composition replay and direct cross-architecture comparison. **Zero pixel/bitmap geometry/order mismatches.**
- Both filter Main-page/option checks and all six DLL targets compile. Matching final sources, EXEs/DLLs and PDBs are verified. No registry writes, player settings or playback changes.

## Rejected prototype and remaining limits

The first opaque-getter storage variant preserved pixels and reduced isolated parse/reconstruction CPU 3.9%, but full-render CPU regressed 2.7%. Its packet medians improved zero lead 5.3% and 250 ms lead 11.6%; this mixed result was not accepted. The final variant moves the forward link into each stable tag and exposes/forces inline hot iteration and first-argument access. The `opaque-*` binaries/sources/measurements and initial pilot are preserved separately, not pooled into final results. One intermediate compile caught the old destructor alias after that structural move; it was corrected before tests.

Smaller storage does not establish zero dropped frames. Requests still exceed the **41.708 ms** source period, and medians do not bound worst-case latency. Numeric spill arrays and unique parameter-string buffers still allocate; grouping topology/key reuse, short-span/dispatch work and actual provider preparation cadence remain separate paths. Preserve exact legacy empty/reset/relative-size behavior, first-closing-parenthesis syntax, CRT conversions and ownership for any next representation change. No playback test was run. See [the architecture map](knowledge-graph.md), [benchmark instructions](../test/benchmark/README.md) and [remaining-work notes](optimization-review-later.md).
