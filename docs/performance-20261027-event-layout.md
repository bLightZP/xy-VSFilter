# General ASS event preparation and layout reuse, 2026-10-27

This implements a conservative part of item 1 in the optimization review.
Eligibility follows ASS operations and rendering dependencies. There are no
conditions on filenames, titles, timestamps, dialogue text or named styles.
The sample is one regression/benchmark input; synthetic text/drawing workloads
exercise the same production code independently.

The accepted candidate reduces median full-render CPU 6.08% on x64 and 2.26%
on Win32. The fixed x64 burst cohort renders 2.72% faster; Win32 burst time
is 0.78% higher. All pixel gates pass on both architectures. Large individual
spikes remain; this is not completion of every item-1 proposal.

## Implementation

The first render evaluates the existing parser and constructs the exact legacy
layout. Eligible events retain immutable raw-word and finalized-line prototypes,
line metrics, wrapping, margins and placement bounds. Polygon prototypes own
immutable script-scaled paths, so later copies share vertices. Prototypes are
never painted. A separate bounded last-appearance cache owns rendered words;
changing their style creates replacements and preserves earlier owners.

For time-based, uniform ASS spans, the renderer can retain static event state
and replay only layout-independent operations: color/alpha, clipping, Gaussian
and edge blur, shadows, rotation and shear. It uses the existing ParseSSATag
cases, numeric values and CalcAnimation arithmetic. Raster geometry is still
computed for the current properties; glyph rendering and pixel kernels are
unchanged. Root static commands establish the template once. Move/fade effects
continue using the requested time in ParseScript.

Font names, sizes, weight/decorations, character sets, spacing, font scales,
outline widths, drawing mode/baseline, wrapping and alignment remain layout
dependencies. Layout-changing transforms, style resets, mixed inline styles
and HTML use the legacy evaluation/construction path. Uniform spans can contain
multiple words, hard/soft breaks and NBSP; text and ASS drawings are supported.
Unsupported/oversized cases retain the original behavior automatically.

A prepared event validates its complete source style, text, entry effect and
margins, script size, PAR mode/value, default wrap mode, border-scaling mode and
duration. Source edits, geometry changes and backward seeks invalidate cached
state. Constant animation intervals retain that context guard; a focused test
caught and fixed stale interval reuse during default-style changes.

All first-use preparation, prototype copies, admission and cleanup execute
inside the measured render/parse calls. No work is moved into startup, an
untimed prepass or a background worker. No lookahead prewarming is implemented.
Both DirectVobSub's adapter and XySubFilter's provider use the shared RTS path.

## Ownership and bounds

Existing syntax admission remains 16 MiB per renderer. Prepared layouts have a
separate 32 MiB admission budget, a 512 KiB per-event cap, at most 128 raw words
and 128 lines, and a bounded font-name size. Accounting includes referenced
strings/paths, vector capacity, prototypes and reserved last-appearance words,
paint keys and opaque boxes. Referenced data is conservatively charged even
when shared. The existing 32 MiB/8,192-entry reconstructed-state policy also
charges prepared states/contexts. The empty-subtitle pool remains below 1 MiB.
These are ownership/admission bounds, not a process-memory ceiling.

Plans expire with their source events. Only one rendered appearance is retained
per plan; older words are never restyled in place. All caching is renderer-owned
and follows the existing synchronous locking model. There are no new workers.

## Measurements

One render thread, CPU 7, affinity 0x80 and verified Above Normal priority.
Builds are serial on that CPU at Normal priority and never overlap timed runs.
Both architectures have four complete passes per build across two ABBA batches.
Values below are medians of complete-pass totals; burst rows sum the same fixed
17 new-event frames. Architectures were measured at different times, so compare
each candidate to its own contemporary control.

| Scope / metric | Control (s) | Candidate (s) | Change |
| --- | ---: | ---: | --- |
| x64 full-render thread CPU | 29.6953 | 27.8906 | 6.08% lower |
| x64 full-render wall | 29.4574 | 27.8974 | 5.30% lower |
| x64 parse phase within full render | 4.6824 | 4.3129 | 7.89% lower |
| x64 burst parse phase | 0.7450 | 0.7187 | 3.53% lower |
| x64 burst render | 1.3734 | 1.3360 | 2.72% lower |
| Win32 full-render thread CPU | 27.6562 | 27.0312 | 2.26% lower |
| Win32 full-render wall | 27.7023 | 27.0435 | 2.38% lower |
| Win32 parse phase within full render | 4.7348 | 4.3570 | 7.98% lower |
| Win32 burst parse phase | 0.7589 | 0.7620 | 0.42% higher |
| Win32 burst render | 1.3427 | 1.3532 | 0.78% higher |

Both x64 batches reduce CPU (5.08% and 5.49%); both Win32 batches also reduce
CPU (2.37% and 1.56%). Win32 burst time is slightly higher in aggregate, so this
batch does not establish a spike improvement on x86. The x64 cohort improves
modestly, while first-use parsing/construction still dominates large spikes.

x64: median per-pass p95 27.719 -> 26.218 ms; p99 55.379 -> 55.070 ms; maximum 113.752 -> 114.187 ms.
Frames over the 41.708 ms source budget using per-frame medians: 39 -> 38.
Median peak process commit: 423.215 -> 419.846 MiB.

Win32: median per-pass p95 25.874 -> 25.137 ms; p99 53.571 -> 53.113 ms; maximum 113.738 -> 116.025 ms.
Frames over the 41.708 ms source budget using per-frame medians: 37 -> 37.
Median peak process commit: 407.936 -> 407.076 MiB.

The largest frame times remain essentially unchanged and above budget. These
offline counts are not playback drops. Process commit is a broad heap/cache
metric, not the footprint of the prepared layouts. Phase times are nested and
must not be added. PC activity and frequency variation remain possible.

Separate real-clip parse-only replay: CPU 5.1875 -> 3.8750 s (25.30% lower).
This mode does not warm raster/path caches through drawing, so its cache
history and timing scope differ from the integrated parse phase above.
Independent 200-event, two-font synthetic parse-only load: 16.4844 -> 0.7656 s (95.36% lower).
That workload favors reusable text layout and raster-only animation; the
result is not a full-render or general-video speed-up claim.

The first two layout-only prototypes did not establish a full-render win;
color/clip-only prepared replay was approximately flat. Their raw results
remain separate in the portable data. They were not promoted.

## Validation and compiled artifacts

Both Release/v145 architectures passed:

- All 22 focused contracts, both actual filter option/Main-page factory
  checks and all 54 fixture cases.
- All 13 extended ASS scripts at every one of the 2,428 timestamps,
  including the new varied event/layout fixture.
- The entire sample at 256 and zero bitmap-cache entries, composition
  replay, and the x64 diagnostic-enabled full pass against original goldens.
  Every timed full-render pass also matches its frozen control.
- Direct x86/x64 comparison of all 2,428 sample frames: zero mismatches.
- Fifteen additional warm rendered frames from the independent 200-event
  synthetic workload against the old x64 renderer: zero mismatches.

`--prepared-layout-contract` compares the feature with caching disabled.
It covers text/drawings, wrap modes and breaks, clips, color/alpha, blur,
shadow/3D rotation/shear, karaoke, opaque boxes, fallback cases, source/style
edits, seeks, expiry, retained frames and byte admission. Visible bitmap
counts, positions, dimensions and bytes match; bitmap IDs/pitch padding
are excluded from pixel hashes.

All six DLLs remain version 3.2.1.900. VSFilter, XySubFilter, consumer and
console binaries/PDBs for both architectures are compiled, with matching
source snapshots, verified hashes and correct PE architecture. The normal
outputs under `bin/lib_18.0/<architecture>/Release/` match the stable
snapshots; the x64 timing executable matches the final build. Twenty build
logs contain no compiler/link/debug-data failures. No player was launched,
no filter registration changed and no software was installed.

[Portable measurements and artifact hashes](benchmarks/20261027-event-layout.json).
Local verification: `benchmark-results/event-layout-20261027/verification.json`.

## Remaining item-1 work

Mixed styled-run preparation, layout reuse across metric-changing operations,
immutable event programs for more ASS cases and bounded ahead-of-activation
preparation remain future work. Keep startup/earlier-frame cost and retained
bytes visible when investigating lookahead. First-use parsing/construction still
causes substantial spikes; this feature does not establish zero dropped frames.

Raw runs, rejected prototypes, controls, final binaries/PDBs and source manifests
are local under `benchmark-results/event-layout-20261027/`. The private sample
and extracted fonts remain local.
