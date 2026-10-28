# Native zero-lead activation follow-up — 2026-10-27

Investigated the x64 zero-lead activation reported as 108.77 → 122.72 ms in [the top-three report](performance-20261027-top-three.md). The final change reuses bounded scalar parameter data across distinct ASS override programs. Rendering, command grammar, numeric conversion semantics and the existing event/layout/draw reuse policies remain intact. No playback tests were run.

## Cause, investigation and implementation

The initial diagnostic profile at frame 32780 / **22:47.220** measured about **54 ms parsing**, **3 ms packet insertion**, **23–24 ms draw setup** and **23–24 ms composition** in a roughly 105 ms request. These scopes include overlapping composition subphases. Disabling draw reuse did not fix activation; disabling layout reuse saved only a few milliseconds and worsened following requests. The historical regression was not reproduced consistently, and timings varied substantially. The historical 14 ms delta should not be treated as a stable attribution to one function.

Cold programs repeatedly created identical scalar parameter strings and ran the same conversions even though complete override strings differed in position/timing. A renderer-local cache now has **512 direct-mapped slots**, accepting at most **128 UTF-16 characters** per scalar span. Every hit verifies command identity, length and full raw text. It owns both raw and trimmed strings; CString copy on write protects prior parsed programs after replacement. Storage remains below **512 KiB** per renderer and is released on source change/destruction. Existing 64/32 MiB event/layout limits and image-cache policies are unchanged.

Cached numeric results accept checked ASCII integer/hexadecimal spellings for existing integer/hex commands. Real-valued commands additionally accept integral ASCII tokens with magnitude at most 65,535: these are exactly representable at all supported floating-point precisions/rounding modes. Signed zero is retained from the original CRT result. The eligibility check never substitutes its own numeric value. Fractional, scientific, larger real and noncanonical forms share only text and still call the original CRT conversion in the current locale/rounding environment. Empty/default and named style resets remain dynamic evaluator operations. Parenthesized forms and oversized spans take the original path. `AssTag` storage is unchanged; the parser retains its legacy first-closing-parenthesis and prefix behavior. No filename, style name, timestamp or shape selects the optimization.

The syntax-parser overload of `ParseSSATag` and `GetParsedSSATags` are instance methods so a full-program MRU miss can use this local scalar cache. The shared native renderer serves both DirectVobSub and XySubFilter/consumer; optional libass bypasses it. Version remains **3.2.1.900**.

## Scheduling and comparisons

All final children verified **one rendering thread at Above Normal priority**. Final comparisons use **CPU 5 / 0x20**. Exploratory CPU 7 tests showed substantial timing spread and competing load on that core and its sibling; those results remain separate. An audit caught an unchecked ctypes affinity call in exploratory scripts: those pilots actually remained on CPU 7 despite some CPU 5 labels. Final launchers use a pointer-sized handle, check affinity setter results and reject any child whose log differs. Exploratory/mislabeled runs are not pooled into final tables. Builds are serial at Normal priority, with no build/timing overlap. No software was installed.

Controls are frozen matching executables/PDBs/sources: **earlier G**, before the top-three changes; **latest B**, the last delivered build; and **candidate**, this fix. Zero-lead comparisons use six fresh-process passes per build in balanced three-build orders. The 1,000 ms lead check uses four passes per build in ABBAABBA order. The final 250 ms check uses eight fresh-process passes per build in alternating AB/BA order. It is a fresh comparison after the preceding candidate failed this check; all final passes remain included. Each covers **131 requests**, frames 32700–32830, and includes chronological packet insertion/segmentation plus `RenderEx`; source/style loading is excluded. Bitmap capacity is 256 and lookahead 4 ms. Undelivered events are hidden from the native renderer. Architectures run in separate time blocks; changing load makes fresh within-architecture comparisons more useful than absolute cross-architecture timings. This is a delivery model, not observed splitter cadence or renderer drops.

The first independently initialized request is recorded separately; the recurring activation at frame 32780 is never excluded under the pause/resume exception. All rows must match full-script golden pixels, bitmap geometry and order. Input ASS SHA-256: `1ece3acc51ef4b091a6af7ab9f87d989e314c15d95e25ed883283d47f9656d72`.

## Zero-lead activation at 22:47.220

| Architecture / CPU | Earlier G | Latest B | Final | Latest → final | Activation + next 3 requests, latest → final |
| --- | ---: | ---: | ---: | ---: | ---: |
| x64 / 5 | 103.10 ms | 101.37 ms | 98.85 ms | +2.5% less | 224.50 → 220.42 ms |
| Win32 / 5 | 114.83 ms | 112.55 ms | 110.26 ms | +2.0% less | 256.27 → 249.07 ms |

Values are medians of per-pass request/window quantities. Raw samples are retained in [the portable JSON](benchmarks/20261027-zero-lead.json), so small differences can be read against spread. The four-request window includes offsets 0, +41.708, +83.417 and +125.125 ms.

| Architecture / CPU | +41.708 ms, latest → final | +83.417 ms | +125.125 ms | +166.833 ms |
| --- | ---: | ---: | ---: | ---: |
| x64 / 5 | 41.68 → 41.34 ms | 43.47 → 43.01 ms | 37.49 → 37.12 ms | 38.45 → 36.43 ms |
| Win32 / 5 | 46.93 → 47.84 ms | 48.39 → 48.20 ms | 44.15 → 40.99 ms | 42.07 → 37.88 ms |

| Architecture / CPU | Latest B range / mean | Final range / mean |
| --- | ---: | ---: |
| x64 / 5 | 101.14–104.78 / 102.14 ms | 97.90–111.04 / 100.73 ms |
| Win32 / 5 | 109.62–119.57 / 113.12 ms | 104.93–121.41 / 111.49 ms |

Ranges and means retain the effects of slower passes rather than discarding them. A lower median does not establish improvement in every activation or a bounded worst case. Coarse thread-CPU counts are also retained; slower wall times must not automatically be attributed to preemption.

## Packet lead checks

| Architecture / lead (CPU 5) | Activation latest → final | Window latest → final | Interval thread CPU latest → final |
| --- | ---: | ---: | ---: |
| x64 / 0 ms | 101.37 → 98.85 ms | 224.50 → 220.42 ms | 2.391 → 2.391 s |
| x64 / 250 ms | 80.69 → 76.79 ms | 205.08 → 199.20 ms | 2.445 → 2.391 s |
| x64 / 1000 ms | 57.13 → 57.78 ms | 178.76 → 178.88 ms | 2.484 → 2.469 s |
| Win32 / 0 ms | 112.55 → 110.26 ms | 256.27 → 249.07 ms | 2.742 → 2.711 s |
| Win32 / 250 ms | 89.96 → 87.39 ms | 229.12 → 226.55 ms | 2.797 → 2.781 s |
| Win32 / 1000 ms | 64.18 → 61.96 ms | 198.73 → 204.51 ms | 2.773 → 2.742 s |

The final checks are not uniformly faster: x64 at 1,000 ms lead has a 0.65 ms slower activation median, and the Win32 four-request window at that lead increases 198.73 → 204.51 ms despite a faster activation. These adverse results remain included.


## Diagnostic isolation

The following one-pass feature-on/off diagnostics use the same final executable on CPU 5. They identify work and confirm exact output; their timings are not pooled into the uninstrumented repeated comparison.

| Architecture / scalar cache | Parse at activation | Draw setup | Bitmap stage | Scalar text hits / misses |
| --- | ---: | ---: | ---: | ---: |
| x64 / on | 49.21 ms | 22.29 ms | 15.86 ms | 28633 / 6011 |
| x64 / off | 52.72 ms | 22.72 ms | 16.05 ms | 0 / 0 |
| Win32 / on | 50.23 ms | 21.29 ms | 14.84 ms | 28633 / 6011 |
| Win32 / off | 58.14 ms | 21.96 ms | 15.60 ms | 0 / 0 |

## Full interval and exact-output gates

- **x64, full 2,428-frame interval:** thread CPU 22.078 → 21.469 s (+2.8% less), four passes per build on CPU 5 at Above Normal; every frame matches. Full `RenderEx` timings include bounded preparation. Thread CPU counters are coarse per request; these are medians of pass totals. This diagnostic full-render comparison is separate from uninstrumented packet timings.
  Median per-pass p99 full request: 36.44 → 35.70 ms.
  Median per-pass maximum full request: 64.96 → 63.37 ms.
- **33 focused contracts per architecture**, including 1,200 unique scalar programs, collisions/eviction, empty/reset/parenthesized/malformed/nested forms, owned strings, default styles, locale, rounding and Win32 24/53-bit precision changes, exact-real boundaries and signed zero, disable, seeks/expiry and retained output.
- **54 original fixtures**, **16 extended scripts × 2,428 frames** per architecture, including the independent 256-event scalar fixture; full private sample with 256 and zero bitmap-cache entries; diagnostics/composition replay and cross-architecture comparison. **Zero pixel mismatches**.
- Both filter Main-page/option checks, all six native DLL targets and both consoles compiled. Matching sources, executables/DLLs and PDBs are frozen under `benchmark-results/zero-lead-20261027/`. No registration, player setting or playback changes.

## Rejected experiments and remaining limits

Keeping two numeric values inline gave only a small pilot gain; four slots enlarged every tag and regressed. Packing numeric values beside parameter strings also failed to improve activation reliably. A deadline-limited admission prototype increased total work without a dependable activation gain. A broader scalar numeric cache won an early pilot, but per-program locale/FP-context probes regressed final activation. All these prototypes were rejected. A narrower integer-command-only cache passed exact-output checks but regressed the 250 ms lead comparison by 14.5% on x64 and 6.2% on Win32; its sources, binaries and measurements are preserved under `integer-only-*`, and are not pooled with final results. The final refinement also reuses exact small integral values in real-valued commands without context probes. Its preliminary four-pass x64 comparison improved zero lead 103.85 → 100.50 ms and 250 ms lead 83.46 → 78.49 ms; the tables above use fresh final passes instead. Their logs/snapshots remain local and are not pooled into accepted results.

Requests still exceed the **41.708 ms** source-frame period. This follow-up targets the regression and repeated cold parsing; it does not establish zero dropped frames. Remaining work includes cold parsed-tag/parameter ownership and allocation, verified grouping topology/key reuse and composition/dispatch costs. Actual end-to-end drops and packet cadence require a separate Debug Zoom Player test; none was run. See [the benchmark README](../test/benchmark/README.md) and [the architecture map](knowledge-graph.md).
