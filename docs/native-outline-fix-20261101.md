# Native outline fix and validation — 2026-11-01

The native outline sweep now preserves an existing left arc when its intersection
result is `INT_MAX` (no intersection), instead of adding that sentinel to the scan
row and overflowing its expiry. This addresses the crash isolated from
`broken subs 2.mkv`; see the [investigation](brokensubs2-native-outline-investigation-20261101.md).

## Implementation

- Preserve the optimized arc-queue sweep, ellipse kernels and lookup/analytic
  intersection paths. Handle sentinels before row arithmetic on both sides.
- Validate queue occupancy, expiry and ellipse indices before access. Bound the
  final sweep by the last source row plus radius, rather than `INT_MAX`.
- Validate radii and source spans in the existing input traversal. The radius
  ceiling is the supported maximum output dimension in 1/8-pixel scan units.
- Publish an ellipse only after successful initialization, propagate cross-table
  allocation failure, and use 64-bit arithmetic for the table-area decision.
- Clear partial output on failure. Throw `CAtlException` for the existing native
  `RenderEx` HRESULT boundary; MFC allocation exceptions are deleted and converted
  to `E_OUTOFMEMORY`. Using `AtlThrow` here would throw an MFC exception pointer
  rather than the exception type caught by that boundary.

Production changes are confined to `src/subtitles/xy_widen_region.cpp`. Parsing,
alpha evaluation, libass, layout caching, draw reuse and bitmap pooling are
unchanged. Both native filter paths use this widener.

## Focused regressions

The regression fixture contains three vector triangles and no dialogue/font
dependency. It retains the failing time and geometry, moves the position down
and removes clipping to make successful rendering visible. The test rejects
silent disappearance as well as crashes and hangs.

`run_outline_regressions.py` runs a direct span-union/recovery contract and actual
XySubFilter/VSFilter DLL contracts under 60-second child watchdogs. XySubFilter
coverage includes 1080p/4K/8K geometry, custom 8K layout with 4K output, cold
requests, border-transform boundaries, backward seeks, 128 retained frame hashes,
EOF requests and filter destruction/recreation. VSFilter coverage uses its CSRI
native entry point with a real 4K pixel buffer, seeks and close/reopen.

The independent union reference shares only the rasterized ellipse kernel. It
does not impose new coverage rules on eccentric overlapping ellipses, where the
existing analytic intersection and Bresenham kernel can differ by a scan unit.

## Validation record

Release builds of all three DLL targets and the console harness succeeded on
x64 and Win32 using the installed VS18/v145 toolchain. The focused outline tests
and 41 existing contracts pass on each architecture, including the 3,600-comparison
alpha matrix, native/libass queue contracts and both filters' layout contracts.
The existing 54 fixture hashes and all 536 frames of `brokensubs1.mkv` match 940
in native default, native legacy-alpha and libass modes.

Both archived 940 x64 DLLs fail the new visible three-triangle fixture with access
violation `0xC0000005`; the candidate DLLs render it and complete seek/teardown
checks. The released Win32 DLLs pass these exact reduced-fixture requests, so the
negative control establishes the reproducer specifically on x64; the Win32 run
provides compatibility and lifecycle coverage. The direct contract also checks
32 span unions and rejected-input recovery.

Full-script x64 native probes using the production widener passed:

| Geometry and schedule | Completed requests | Peak private memory |
| --- | ---: | ---: |
| 4K raster/layout, full midpoint schedule | 2,159 | 215.8 MiB |
| 8K raster/layout, full midpoint schedule | 2,159 | 596.0 MiB |
| 8K layout / 4K raster, frame-start schedule | 2,159 | 217.7 MiB |
| 8K raster, 4 ms earlier than midpoint near 30 seconds | 71 | 272.3 MiB |
| 8K layout / 4K raster, alternating seeks/end, retaining 128 frames | 160 | 137.6 MiB |

Every probe exited normally, including renderer teardown. These are console
tests, not observed madVR queue depths or playback throughput.

One initial queue-contract attempt overlapped the full-sample CPU-7 probe and
missed its existing 100 ms asynchronous notification deadline. The isolated
serial rerun passed; production queue code is unchanged. Timing comparisons
are run serially without those competing probes.

Generated binaries, logs and results are under
`benchmark-results/outline-fix-20261101` (`validation.json`, architecture-specific
`outline.json`, and `sample-matrix.json`). Full-sample phase CSVs also reside in
`benchmark-results/brokensubs2-20261101`. These measurements were collected
before the 3.2.1.950 version-resource rebuild.

The final binary/source hashes are in `candidate-manifest.json`; its archived
DLLs match the build outputs. Test runner hashes separately identify the binaries
used by the focused DLL regressions.

### Performance and preserved output

Fresh processes ran serially on CPU 7 at Above Normal priority, in the order
940/candidate/candidate/940. The credits comparison covers 2,428 frames per pass;
all bitmap counts, positions/sizes and pixel hashes agree across both versions
and with the existing reference, on both architectures. Values below are medians
of the two passes per version, not statistical confidence bounds.

| Workload | 940 total render time | Candidate | Change | p99 frame time, 940 → candidate |
| --- | ---: | ---: | ---: | ---: |
| Credits, x64 | 20,979 ms | 21,074 ms | +0.45% | 35.40 → 36.21 ms |
| Credits, Win32 | 20,782 ms | 21,313 ms | +2.56% | 34.40 → 36.30 ms |
| Sample's 8K effect window, x64, 71 requests | 2,526 ms | 2,593 ms | +2.67% | 96.68 → 100.65 ms |

The 8K timing window uses a midpoint phase on which the old code completes; the
separate 4 ms-offset regression covers the formerly failing phase. Its small
71-frame set makes p99 equal to the maximum observed request. The first safety
implementation cost about 5.6% in this window. Reusing the sweep's existing last
retained arc index for its capacity check removed an additional modulo operation
and reduced that measured cost to 2.7%, without dropping the guard.

These measurements show a small safety-check cost, not a zero-cost fix. The
existing performance optimizations and cache policies remain in place. No UI
automation or playback was run during these timing passes. Raw measurements are
in `performance.json` and `effect-performance.json` beside the per-frame CSVs.

### Release gate: actual madVR playback

The user closed the existing player, allowing the required MAX Debug/LOCAL
player and both helpers to rebuild successfully. Actual playback uses madVR
0.92.17.0, the sample's default embedded ASS track, native rendering, software
video decoding and a 128-entry subtitle queue. The loaded candidate DLL and
active helper hashes are checked against the archived candidate and Debug build
record. The graph log confirms XySubFilter feeding madVR.

The x64 runs completed the entire 90-second sample with both custom 7680×4320
and 3840×2160 subtitle layouts. Each seek run first performs 26 paused seeks:
three cycles through 30, 31, 32, 70.633, 71, 89, 31 and 0 seconds, then another
31-second check and a return to zero. The full playback pass follows without
further seeks. Both runs reached EOF, closed gracefully and logged a normal
helper exit. No forced termination was required.

The Win32 candidate also completed the same 26 seeks and full sample at custom
3840×2160, with the matching Debug 32-bit helper. It reached EOF and exited
normally. Its paused screenshot shows visible subtitles and a recent queue
range of 111–128/128 after seeking. Across the three seek runs, all 78 seeks
completed and the slowest observed position reply was 321 ms.

Screenshots confirm a 3840×2160 presentation surface, visible subtitles after
seeking to 31 seconds and a subtitle queue reaching 128/128 while paused. The
8K-layout capture shows a recent range of 114–128/128 after seeking; the 4K
capture shows 128–128/128. An earlier uninterrupted 8K-layout run also completed
and showed 127–128/128 during playback. This is an oversized window on the local
2560×1440 display, not a physical 4K-monitor test; madVR's overlay confirms its
actual 3840×2160 scaling target.

These are stability and queue checks, not controlled playback-speed comparisons.
The seek-run measurements are:

| Helper / subtitle layout | Maximum seek reply | Peak helper private memory | Final dropped-frame counter |
| --- | ---: | ---: | ---: |
| x64 / 8K | 286.3 ms | 4,044.6 MiB | 10 |
| x64 / 4K | 320.2 ms | 4,079.9 MiB | 9 |
| Win32 / 4K | 307.5 ms | 3,316.0 MiB | 17 |

Memory here covers the entire playback helper, including madVR, decoder and
graphics-driver allocations; it is not the standalone subtitle renderer's
memory measurement above. There is no claim of zero-drop playback. Paused redraws inflate TCP 1150's
average-FPS counter, so it is not used as a throughput result. The isolated
render-time comparisons above provide the performance comparison.

The first setup attempt stopped because the collector checked subtitle selection
before the external helper's asynchronous reply arrived. The local collector
now waits up to 10 seconds for the requested track. That attempt did not enter
the test; subsequent runs confirmed subtitle index 0 before continuing.

Local evidence is under `benchmark-results/outline-fix-20261101`: Debug build
summary in `zoom-debug-build/MAX/Debug-summary.json`; playback metadata, TCP and
CPU samples, graph/teardown logs and screenshots in `madvr-x64-8k-02`,
`madvr-x64-8k-seeks`, `madvr-x64-4k-seeks` and `madvr-x86-4k-seeks`.
`madvr-validation.json` summarizes all four completed playback passes.
`run_madvr.py` and the local
collector copy record the exact validation procedure. Temporary COM registration,
filter layout, decoder, queue and player settings are restored after each run.
The final live registry values and original player-settings bytes were also
checked after the last run; no benchmark player/helper remains running. The
madVR stability/queue/EOF/close gate for this sample is complete.

## Release 3.2.1.950 verification

The versioned release rebuild passes all three new outline contracts and 43
existing contracts per architecture, including actual-DLL property/version,
layout and native/libass queue checks. All 54 fixtures and 536 frames in each
native-default, native-legacy-alpha and libass mode match 940 on Win32 and x64.
Both streaming consoles also build and match the 50-frame reference; their
shared frame-hashing call was updated to pass the explicit interface pointer.
All 11 Python benchmark-tool tests pass. The production widener hash still
matches the candidate used for the full-script and performance checks above.

The exact 950 x64 DLL completed an additional madVR pass with custom 8K layout
and 4K presentation: 26 seeks, visible subtitles and a paused queue range of
125–128/128 at 31 seconds, full playback to EOF, and normal player/helper exit.
The final dropped-frame counter was 19; this remains a stability check,
not a claim of zero-drop playback. The slowest seek reply was 304.8 ms.
Temporary filter, decoder, queue and player settings were verified restored.
The required Debug player was rebuilt for this final check; the pre-test player
executables and maps were restored afterward.

Release build logs, version checks, candidate hashes and results are local under
`benchmark-results/release-v3.2.1.950`; `validation.json` includes the final madVR
summary, while `madvr-x64-8k-final-02` contains the detailed capture and logs.
