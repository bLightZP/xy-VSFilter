# General native ASS spike reduction, 2026-10-27

The accepted build combines event preparation, bounded memory, active-set
maintenance, allocation and exact composition changes. Eligibility is based
on ASS operations and source/context validity; production code recognizes no
sample filename, title, timestamp, dialogue content or style name. Both native
DirectVobSub and XySubFilter adapters use the same shared implementation.

The console results below show materially lower spikes, while some frames
still exceed the source's 41.708 ms budget. No playback testing was performed,
and these offline counts are not measured dropped frames. The full-script
comparison supplies future events in advance: a separate packet model below
shows that lookahead provides no benefit when future packets are unavailable.

## Implementation and ownership

1. **Timed preparation before activation.** `RenderEx` traverses upcoming
   segment boundaries synchronously after constructing current output, or on
   a blank request. It evaluates eligible upcoming events at their initial
   state, retaining only states with the existing complete source/style/context
   guard. Current timestamps still drive actual rendering; unsupported states
   are recycled and retain the legacy fallback. Static/mixed/metric-changing
   events can benefit from immutable syntax/path work without retaining an
   unsafe completed state. There is no worker, untimed prepass or startup work
   moved outside the benchmark.
2. **Bounded active-event memory.** Default syntax admission increases from
   16 to 64 MiB and reconstruction from 32 MiB/8,192 entries to 64 MiB/16,384.
   Immutable layouts remain 32 MiB with their existing per-event limits; the
   empty subtitle pool remains below 1 MiB. This lets current and upcoming
   events coexist. Raster and bitmap cache capacities are unchanged. Accounted
   admissions are not a process-memory ceiling; measured commit is separate.
3. **Verified order and expiry deadlines.** Consecutive requests reuse qsort
   results only after comparing the complete source membership, layers and read
   orders. Streaming insertion and order edits are detected. Retained arrays
   cover at most 32,768 events and stay below 1 MiB including growth slack;
   larger sets use call-local storage. TIME-mode expiry scans run at the next
   admitted end point; FRAME mode retains per-call scanning for FPS changes.
   This is not a new incremental ordering or heap-expiry algorithm.
4. **Less allocation during drawing/grouping.** One shared allocation contains
   a word paint machine and its three layer wrappers. Returned layer aliases
   own that allocation; embedded wrappers borrow their parent without a strong
   reference, so no cycle exists. Dirty geometry/layer sharing stays intact.
   Duplicate vertical boundaries are removed before constructing the existing
   grouping bands; the sweep, coalescing, bitmap partition and layer order stay
   unchanged.
5. **One owning interner probe.** `FindOrAddOwned` traverses one hash bucket,
   invokes an owning key factory only on a miss, and preserves MRU behavior,
   identities, zero-capacity bypass and retained borrowed-key ownership.
6. **Weak clip-mask reuse.** A clipper machine avoids repeated mask-cache
   lookups while the bounded MRU or a caller owns its painted mask. A weak
   reference permits actual release on eviction; it adds no image owner pool.
7. **Exact fused packed composition.** Single-color packed alpha blending
   generates coverage in SSE2 registers and immediately blends it, eliminating
   intermediate alpha-image writes/reads. Body/border subtraction, vector mask
   multiplication, fixed-point rounding and clipping match the old routines.
   Exact zero-coverage and fully opaque endpoints skip unnecessary blending.
   Planar, multicolor and additive paths keep their existing implementation.

Preparation has a one-second horizon, up to 4 ms allowance per request and a
deadline at 75% of the source frame period. The deadline is checked between
source events; one event is non-preemptible and can overshoot it. The path skips
ANSI entries and source strings over 8,192 characters, and caps its attempt
index at 1,048,576 entries. Admission budgets bound retained plans separately.
Source changes, seeks and deinitialization reset the relevant state. Provider
request cadence affects opportunities: a playback adapter may skip blank/static
requests that the console makes. These results do not predict identical
preparation opportunities in playback.

## Measurements

One serial render thread on logical CPU 7, affinity 0x80, verified Above Normal
process priority. Builds run serially on that CPU at Normal priority with
`/m:1 /nr:false`, and never overlap timed runs. Both architectures have four
complete passes per build across two ABBA batches. Each pass renders the same
2,428 frame midpoints from 22:20 through EOF at 1920x1080. Controls are the
previous accepted event/layout build; x64 owned grouped keys and x86 borrowed
keys remain unchanged.

Values are medians of complete-pass totals. Burst rows sum the same 17 frames
selected from event-start counts before looking at candidate timings. A
positive percentage means less elapsed/CPU time.

| Scope / metric | Control | Candidate | Reduction |
| --- | ---: | ---: | ---: |
| x64 thread CPU (s) | 28.562 | 24.758 | 13.32% |
| x64 complete render wall (s) | 28.560 | 24.773 | 13.26% |
| x64 parse phase (s) | 4.432 | 2.926 | 33.99% |
| x64 draw construction (s) | 6.345 | 5.953 | 6.17% |
| x64 bitmap composition (s) | 11.953 | 9.631 | 19.43% |
| x64 burst parse (s) | 0.728 | 0.165 | 77.28% |
| x64 burst complete render (s) | 1.357 | 0.733 | 45.94% |
| Win32 thread CPU (s) | 27.891 | 24.664 | 11.57% |
| Win32 complete render wall (s) | 28.182 | 24.989 | 11.33% |
| Win32 parse phase (s) | 4.539 | 3.145 | 30.70% |
| Win32 draw construction (s) | 6.805 | 6.242 | 8.27% |
| Win32 bitmap composition (s) | 11.408 | 9.498 | 16.74% |
| Win32 burst parse (s) | 0.786 | 0.169 | 78.49% |
| Win32 burst complete render (s) | 1.382 | 0.716 | 48.22% |

The full-render timer includes lookahead, including blank requests. Its nested
parse phase excludes lookahead, so that phase's reduction partly reflects
moving setup into earlier measured calls and must not be presented as the
complete saving. Nested phase totals overlap and must not be added.

Tails are medians of per-pass wall-time statistics. Budget exceedances use
per-frame medians across the four passes; they are not playback drops.

| Metric | Control | Candidate | Reduction |
| --- | ---: | ---: | ---: |
| x64 p95 (ms) | 26.599 | 23.991 | 9.80% |
| x64 p99 (ms) | 56.134 | 39.582 | 29.49% |
| x64 per-pass maximum (ms) | 115.179 | 65.757 | 42.91% |
| x64 frames above 41.708 ms | 38 | 16 | -- |
| x64 peak process commit (MiB) | 421.06 | 441.09 | -- |
| Win32 p95 (ms) | 26.076 | 23.812 | 8.68% |
| Win32 p99 (ms) | 55.260 | 38.622 | 30.11% |
| Win32 per-pass maximum (ms) | 117.805 | 74.769 | 36.53% |
| Win32 frames above 41.708 ms | 38 | 15 | -- |
| Win32 peak process commit (MiB) | 406.37 | 412.51 | -- |

An independent two-font workload has twelve 64-event bursts, drawings, styled
runs and raster/metric animation. Across its 323-frame timing region, median
thread CPU changes 13.688 -> 12.711 s
(7.13% lower). This fixture favors preparation and
composition opportunities and is not a universal speed-up claim. Every timed
frame matches the old renderer; its full 2,428-frame regression is also checked
on both architectures.

Frequency changes, PC activity and cache interference remain possible despite
affinity/priority. No playback CPU/drop claim is made. Exact raw per-pass
statistics, cache counts, preparation units, memory and artifact hashes are in
the [portable data](benchmarks/20261027-spike-paths.json).

## Packet delivery and cold-start limits

The complete-script comparison above makes all future events available. That
is useful for external ASS files, but embedded packets may arrive closer to
their presentation time. A separate console probe retains decoded packets in
its test driver, removes all native entries/segments, and delivers them only
when `start <= now + lead`. The production renderer cannot inspect the driver's
future list. Every relevant `Add`/segmentation call and `RenderEx` is timed;
dataset reading/decoding, sorting and style setup are outside this model.

The x64 model covers 131 frames (32700..32830), including the preselected
32780 burst. It compares lookahead disabled/enabled within the same optimized
build at the matching 256-entry bitmap capacity, with four passes per mode
per lead in alternating ABBA order. These are not old/new renderer comparisons
and are not observations of actual DirectShow packet delivery or threading.
The first request includes cold active-event loading, recorded in the complete
results. The steady maximum below excludes only that first model request. All
later event activation and first-use/font/raster frames remain counted. The user
accepts an isolated pause-resume drop; this model does not perform an actual
pause/resume test, and its initialization is reported separately.

| Packet lead (ms) | Selected burst total ms, off -> on | Median steady maximum ms, off -> on | Full-window thread CPU saving |
| ---: | ---: | ---: | ---: |
| 0 | 103.04 -> 104.13 | 103.04 -> 104.13 | -1.14% |
| 250 | 99.50 -> 79.53 | 99.50 -> 79.53 | 3.07% |
| 1000 | 98.67 -> 59.30 | 98.67 -> 59.30 | -0.28% |

With zero lead, all four enabled passes prepare zero future events and show
no established saving. At 250 ms, the selected burst improves about 20%; at
one second it improves about 40%. The recurring maximum follows those bursts;
it still exceeds 41.708 ms. Complete-window maxima, retained in the portable
data, remain around 108 ms due to the initial cold request. That initialization
is separate from recurring playback acceptance. CPU is
approximately flat at zero/one-second lead; the 250 ms case saves about 3%.
Do not present the selected-burst reduction as a reduction of every cold frame.

All 24 x64 passes (3,144 frames) match the full-script original gold, and a
separate 131-frame Win32 packet pass also matches. Both probe architectures
are compiled with frozen binaries, PDBs and source manifests. The current CLI's
custom frame/burst/allowance arguments also pass a small functional check.
The early 64-entry exploratory run is preserved separately and is not pooled
with the matching-capacity comparisons.

Modeled insertion at the zero-lead selected burst costs about 3 ms, versus
about 100 ms in RenderEx. This does not profile raw packet decoding, pin locks,
demuxing, other graph threads or actual playback scheduling. Current evidence
keeps cold renderer construction/drawing as the stronger next target. Actual
packet lead and provider request cadence must be measured before projecting
these lookahead gains onto the player's embedded track. No player was run.

## Experiments retained separately

The maintenance/interner/weak-clip batch alone was approximately flat in total
CPU, although its parse phase improved. Fused composition improved bitmap time.
Lookahead constrained by the old budgets was also approximately flat overall;
the enlarged-budget/ownership combination improved the dense bursts. The
budget and allocation contributions were not measured independently.
The combined-memory/ownership candidate reduced x64 maxima to roughly 65 ms.
The accepted endpoint fast paths improved composition further.

A subsequent contiguous/inline grouping-band prototype passed all pixels but
was rejected. Paired composition-only ABBA against the accepted code showed
12.91% slower merge time and 4.65% higher thread CPU. It is absent from current
source and final binaries. The retained grouping change only removes duplicate
vertical boundaries before the original ATL bands. Prototype timings are not
pooled with final comparisons.

## Validation and compiled artifacts

Both x86 and x64 Release/v145 builds pass:

- All 28 focused contracts, both actual filter option/Main-page factory checks
  without registration/registry writes, and all 54 small fixture cases.
- All 14 extended ASS scripts at every one of the 2,428 timestamps, including
  the independent burst fixture, Unicode/ANSI, mixed styles, inverse clips,
  polygons, geometry, karaoke, blur and large draw-slot spill.
- The full private sample at 256 and zero bitmap-cache entries, composition
  replay, every timed full-render pass, and the x64 diagnostic full pass.
- Direct x86/x64 comparison of all sample frames. All pixel gates report zero
  mismatches in bitmap counts, positions, dimensions and visible RGBA bytes;
  identities and pitch padding are excluded.

New differential contracts cover every opacity, coverage pairs, arbitrary
destination bytes, masked/unmasked border subtraction, vector/tail widths,
offsets/clips and format/color fallbacks against the legacy pixel routines.
They also cover streaming inserts, order edits, FRAME/FPS, source/style edits
before activation, fallback cases, backward seeks, expiry, retained frames,
one-probe ownership/zero capacity, weak-mask eviction, and all paint-layer
alias combinations without cycles.

All six DLLs remain version 3.2.1.900. VSFilter, XySubFilter, consumer and console
binaries/PDBs for both architectures are compiled and frozen with matching
source/artifact hashes. Normal outputs under `bin/lib_18.0/<arch>/Release/`
match those snapshots. No player was launched, no registration changed and no
software was installed. Local controls, prototypes, logs, CSVs and verification
are under `benchmark-results/spike-paths-20261027/`; private media/fonts remain
local. No commit, push or release publication is performed for this batch.

A focused backward-seek/static-cache/style-change sequence first reproduced
a mismatch in the lookahead prototype. Preparation now preserves any existing
cached state, and that sequence is part of the passing lookahead contract.

## Every original review path: current boundary and remaining work

| ID | Current implementation | Remaining investigation |
| --- | --- | --- |
| 1 event/layout | Uniform immutable layout/state reuse plus timed bounded preparation | Mixed styled-run programs, metric-changing reuse; long/unsupported events; cold raster/draw setup |
| 2 grouping/allocation | Compact slots/metadata/members, combined paint allocation, unique bounds | Cross-frame topology/group reuse and a better sweep; inline band prototype rejected |
| 3 active events | Verified order reuse and deadline-gated expiry | Incremental sorting and indexed expiry if profiling justifies their added state |
| 4 composition | Exact fused single-color packed blend | Other formats/color/additive kernels, or smaller clipped working sets, where measured |
| 5 edge blur | Prior unused-image removal and row reuse complete | No further change without a new measured bottleneck |
| 6 caches | Prior coupled sizing/zero bypass plus single-probe owned interning | Unified group/payload storage only if remaining cost warrants it |
| 7 hashes | Prior six-word karaoke hash complete | Fractional geometry canonicalization needs a defined equality/precision contract |
| 8 output validity | Existing reconstruction intervals retained | Full-output/event/group reuse; first reproduce/fix wrapper invalidation before longer validity |
| 9 clipping | Weak per-machine mask reuse | Sparse inverse masks, tighter normal bounds and inverse-region plans; preserve composition/partitioning |
| 10 memory | Explicit bounded active-event headroom; weak clip ownership | Coordinated raster byte budgets, retained-owner and transient accounting under pressure |

Remaining large frames combine event construction, drawing setup, grouping and
bitmap misses. Reprofile those resulting frames before choosing the next
subset. Preparation for unsupported/long events and cold geometry is the next
spike-oriented opportunity; general output/group reuse may help stable phases.
Do not relax subpixel precision, skip animation frames, change ASS semantics
or add parallel workers to satisfy the timing budget. `TextInfoCacheKey` already
excludes rotation/shear from its metric key; the earlier suspicion alone does
not justify changing it.
