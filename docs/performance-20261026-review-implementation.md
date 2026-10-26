# First implementation from the ASS rendering review

Date: 2026-10-26. All three DLL targets and the native harness now build for x86
and x64. Both candidates pass every pixel gate. Eight Debug Zoom Player captures
show small observed CPU reductions, with dropped-frame ranges overlapping their
controls. **Zero dropped frames has not been achieved.**

The [portable measurement summary](benchmarks/20261026-review.json) contains
binary hashes, toolchain versions, capture metrics and the validation inventory.
Raw files under `benchmark-results` are local and ignored by Git.

## Scope and changes

This implements the first localized batch from the
[ASS rendering review](ass-rendering-optimization-review-20261025.md). Both the
control and candidate contain the preceding inline-group/borrowed-probe
experiment. These measurements isolate this batch rather than measuring the
cumulative difference from the published release.

- `CacheManager::SetBitmapCacheSize` couples group-identity and bitmap-payload
  capacities during initialization and runtime option changes. XySubFilter,
  VSFilter, the Avisynth initializer and the native harness call it.
- A zero-capacity MRU retains nothing and returns no position. Flyweights keep
  owned values and assign fresh nonzero identities on bypass, avoiding access
  through the list entry the previous implementation had just removed.
- `DrawItemHashKey` folds all six karaoke switch words in equality order.
  Floating-point geometry hashing and equality remain unchanged.
- Integer edge-blur passes share two operation-local `WORD` rows, reset before
  every pass. Active `BeBlur` no longer allocates an unused image temporary.
  The legacy fixed-point Gaussian branch keeps the temporary it actually reads;
  its edge passes use the reusable rows too. Row indexing avoids forming a
  pointer before its allocation.

For width W, height H, pitch P and N integer passes, active edge scratch changes
from an unused `4*(P+1)*(H+1)` bytes plus two W-word rows allocated on every pass
to `4*W` row bytes allocated once. Fractional-only edge blur needs neither of
those temporaries; its fractional kernel is unchanged. These are source-derived
allocation bounds, not measured process-memory savings or playback speed-ups.

Cache defaults, grouping topology, bitmap partitioning and rendering arithmetic
are preserved. Larger event/layout reuse, incremental ordering/expiry, wrapper
invalidation and composition kernels are separate follow-ups.

## Builds and reproducibility

The user completed the Visual Studio update before these successful clean builds.
All three targets (`VSFilter`, `XySubFilter`, `xy_sub_filter_consumer`) and the
native harness use Release/v145 with symbols. Zoom Player uses Debug MAX/LOCAL.

| Candidate architecture | DLL build errors | DLL build warnings | Native contracts | Small fixture cases |
| --- | ---: | ---: | ---: | ---: |
| Win32/x86 | 0 | 146 | 15 passed | 54 identical |
| x64 | 0 | 166 | 15 passed | 54 identical |

Toolchain: VS 18.11.12324.101, MSBuild 18.11.1.51303, compiler
19.51.36260.0, tools directory `14.51.36231`. Commands were a solution Clean,
then `/t:vsfilter;xy_sub_filter;xy_sub_filter_consumer`, followed by
`test/benchmark/render_subtitles.vcxproj`, each with
`/p:Configuration=Release /p:Platform=<Win32|x64> /p:PlatformToolset=v145`.
Yasm came from the user-supplied `tools/yasm.exe`; no software was installed by
the task. The earlier build interrupted during the update was not used.

A matching control was compiled separately from HEAD
`e61a07fa41d17f23e2ee848354f5aedd9bb616fd` plus the three preserved pre-batch
grouping source files. It uses the same updated toolchain. Both rebuilt controls
also pass all 2,428 sample hashes and the new blur/karaoke fixture against their
preserved originals. This avoids attributing a compiler change to this batch.

The current player and both helpers match the successful 2026-10-25 17:40 Debug
MAX build record, copied to `benchmark-results/zoom-debug-20261026.json`. Their
hashes and PE architectures were verified; no unsigned Release player was used.
The actual helper and selected DLL were verified inside each engine process.

Frozen artifacts, source snapshots and manifests:

- `benchmark-results/review-20261026-Win32/`
- `benchmark-results/review-20261026-x64/`
- `benchmark-results/review-20261026-matched-control-Win32/`
- `benchmark-results/review-20261026-matched-control-x64/`

The candidate folders contain all three DLLs/PDBs and the harness. Both current
candidate source files and every frozen candidate/control binary match their
recorded hashes. Normal build outputs remain under
`bin/lib_18.0/<Win32|x64>/Release/`.

## Pixel and correctness gates

Both architectures passed:

- All 15 contracts, including zero/enable/shrink/disable, retained ownership and
  identity, complete karaoke switch hashing, and 1,000 distinct split positions.
- All 54 small fixture cases against the preserved pre-batch output.
- All 2,428 sample frames at 256 bitmap entries and again with caching disabled
  at zero entries; 2,184 sample frames contain subtitle bitmaps.
- All 2,428 frame positions for each of `reconstruction.ass`, `zero-alpha.ass`,
  `polygon-font-fields.ass`, `geometry-reuse.ass`, `multiline-inverse.ass`,
  `omitted-numbers.ass`, and the new `edge-blur-karaoke.ass`.

**Every comparison has zero mismatches.** Hashes cover bitmap positions,
dimensions and visible RGBA bytes; cache identities and pitch padding are
excluded. The new fixture combines integer/fractional and mixed Gaussian/edge
blur, body-only rendering, wide outlines, tiny drawings, clipping, changing
scale/rotation and progressing karaoke. The eight Python playback-metric tests
also passed.

Some extended-script checks overlapped the separate control build and serve as
correctness gates. No builds or native runs overlapped playback. Offline wall
timings are diagnostic single passes, not replicated speed-up measurements.
At 256 entries the candidate sample reported:

| Architecture | Mean ms | p95 ms | p99 ms | Frames above 41.708 ms |
| --- | ---: | ---: | ---: | ---: |
| x86 | 16.151 | 37.217 | 78.250 | 75 |
| x64 | 15.862 | 36.209 | 82.390 | 65 |

The long tail remains above the source-frame budget even before accounting for
other playback work. These native timings do not map one-for-one to renderer
drops, but support focusing the next work on peak frame cost.

## Debug Zoom Player playback

Eight fresh, unprofiled captures ran in ABBA order per architecture:
control, candidate, candidate, control. Conditions were:

- `Z:\zpAI\zplayer.exe`, verified Debug MAX/LOCAL and matching helpers.
- Default track 0, `Signs & Songs [eng] (ass) [default]`, explicitly selected.
- 22:20 through EOF; each complete capture lasts about 102 wall seconds.
- MPC Video Renderer; main client 1930 x 1090, rectangle `[312,48,2242,1138]`.
- Temporary bitmap/key capacity 256, identical memory-limit settings.
- TCP 1150 at one-second cadence and process-tree CPU/memory sampling.
- x86 uses internal32; x64 uses the verified native external64 helper. Each is
  compared only with its own control. CPU includes the host and helper.

All comparisons passed renderer/dependency hashes, player hash, media, subtitle,
engine, window and polling-cadence gates. Both cache settings were checked too.
No interrupted capture was included. Initial-interval drops are included: the
first Run resets FramesDrawn in all x86 captures and the first x64 control;
these are explicitly recorded while FramesDropped remains continuously zero
across those resets. No drop counter reset or backward position was accepted.

| Capture | New drops | CPU seconds | Peak combined private MiB |
| --- | ---: | ---: | ---: |
| `Win32-control-01` | 48 | 49.6094 | 621.4 |
| `Win32-candidate-02` | 44 | 46.0625 | 451.2 |
| `Win32-candidate-03` | 39 | 48.2344 | 448.0 |
| `Win32-control-04` | 39 | 48.7500 | 451.7 |
| `x64-control-01` | 49 | 52.7344 | 524.9 |
| `x64-candidate-02` | 45 | 51.8281 | 542.7 |
| `x64-candidate-03` | 44 | 51.2812 | 519.0 |
| `x64-control-04` | 45 | 51.1250 | 520.2 |

| Architecture | Control mean CPU s | Candidate mean CPU s | Observed reduction | Control drops | Candidate drops |
| --- | ---: | ---: | ---: | --- | --- |
| x86 | 49.1797 | 47.1484 | 4.13% | 48, 39 | 44, 39 |
| x64 | 51.9297 | 51.5547 | 0.72% | 49, 45 | 45, 44 |

Only two runs per binary/architecture were measured. The CPU averages are
observations, not statistically established gains. Drop ranges overlap, and the
best candidate has 39 drops on x86 and 44 on x64. The zero-drop target is unmet.
There is no established process-memory improvement; the first x86 control is a
large peak-memory outlier. Do not pool these runs with older madVR/smaller-window
results or earlier player builds.

Raw capture folders are `benchmark-results/review-20261026-playback-*`;
architecture comparison JSONs are `review-20261026-Win32-comparison.json` and
`review-20261026-x64-comparison.json` in the same parent. Every capture contains
renderer/CPU CSVs, TCP responses, setup/cleanup evidence and verified modules.

## Cleanup and next work

All eight captures restored temporary COM registration, bitmap capacity and
engine settings. The final four player option values and both registry views
match the recorded preflight exactly: completion action 2, external engine
mode/enabled 1, extension list MP4, bitmap capacity 64, and no per-user filter
registration override. No player/helper, renderer harness, compiler or MSBuild
process remains. Installed Program Files filters and the published v3.2.1.900
release were not replaced.

The next iteration should profile the actual x64 helper and add phase/allocation
counters before selecting a larger change. Cache active-set render ordering and
expiry work with membership/generation validation, then isolate the earlier
inline-group storage from borrowed-key miss overhead. Test same-segment sample
insertion, exact boundaries, seeks and edits. Conservative appearance-only event
reuse can follow once dependency/rejection measurements justify it. Fix and test
the review's wrapper invalidation issue before extending output validity:
reconstruction stability alone is not final-output stability.
