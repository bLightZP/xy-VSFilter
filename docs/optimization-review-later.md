# Optimization work for later review

## Cold tag/parameter storage follow-up

Lazy packed tag nodes, compact owned parameter spills and combined program/control
ownership are implemented in the shared native renderer. Hot iteration and the
first parameter read are explicitly inlined; programs remain immutable and nodes
stay at stable addresses. On x64, nodes including links shrink 128 → 64 bytes;
parameter headers shrink 40 → 24 bytes. Win32 nodes shrink 72 → 40 bytes and
parameter headers 20 → 12 bytes. Common arities 2–7 need 9 spill growths instead
of 20 in the focused storage test. This is not an all-heap allocation count.

See [the complete results](performance-20261028-tag-storage.md) and
[portable measurements](benchmarks/20261028-tag-storage.json). Both architectures
pass 34 contracts, 54 original fixtures, 17 extended scripts, full sample at
256/zero bitmap entries, composition replay and direct cross-architecture pixels.
All six native DLLs compile with matching source/PDB snapshots. No playback ran.
Overall x64 CPU is approximately flat; cold/lead results are modest and mixed,
with the slower long-lead windows explicitly retained in the report.

Unique parameter character buffers and numeric spill arrays still allocate.
Further pooling or source-span/typed representations need exact legacy empty,
reset/relative-font-size, malformed syntax, CRT locale/rounding and retained-owner
gates. Do not infer a safe borrowed string lifetime from a synchronous parse call.
Verified grouping topology/group-key reuse, short-span/dispatch optimization and
preparation during actual provider cadence remain separate outstanding paths.
Continue one core/thread at Above Normal, exact x64/Win32 output and no playback.


## Zero-lead follow-up

The renderer now reuses bounded scalar parameter strings across different ASS
programs, and cached numeric values for invariant ASCII integer/hex forms and
exact small integral real values (magnitude at most 65,535). Fractional, scientific,
larger real and noncanonical forms still use the current CRT environment.
See [the zero-lead investigation and comparisons](performance-20261027-zero-lead.md)
for latest/earlier controls, activation and following requests, raw spread and
rejected alternatives. The old 108.77 → 122.72 ms gap varied with background
load; fresh paired measurements and actual child affinity are required.

Cold tag-object/parameter allocation remains a candidate for reducing zero-lead
activation further. Any compact representation must preserve empty/reset and
malformed-tag semantics, source ownership, CRT conversions and retained frames.
Over-budget composition and grouping work still needs attention.
This change does not establish zero playback drops. Continue using one rendering
thread at Above Normal, exact x64/Win32 output gates, and no playback.

## New-review top-three follow-up

The first three candidates from [the October 27 review](ass-spike-optimization-review-20261027.md)
are implemented: constant drawing-mode eligibility, guarded static vector staging
and the compatible polygon scanner, plus exact draw/bounds reuse with weak raster
owners between requests and strong pins confined to composition. See
[the implementation/results report](performance-20261027-top-three.md).

The remaining review paths are verified grouping topology/group-key reuse,
short-span/dispatch measurement and optimization, and preparation opportunities
in actual provider cadence. Do not assume all events fit the existing 32 MiB
layout budget, or that native console frames over budget equal playback drops.
Keep the isolated pause/resume exception separate from recurring spikes. Use one
rendering thread at Above Normal; no playback is authorized by this follow-up.

## Previous spike-reduction follow-up

See [general spike reduction](performance-20261027-spike-paths.md) and its
[portable measurements](benchmarks/20261027-spike-paths.json). Timed bounded
lookahead, larger active-event admissions, verified ordering/deadline scans,
one-probe owning interning, combined paint allocation, weak mask reuse and
exact fused packed composition are implemented and validated on x86/x64.
The report records final CPU/tail/memory results, all pixel gates, one rejected
grouping prototype and the status of every original review finding.

Some recurring frames remain above 41.708 ms. Offline counts are not playback
drops. The user accepts one isolated pause-resume drop; keep it separate from
recurring activation/first-use spikes. No actual pause/resume test was run.
The packet model confirms that no advance data means no lookahead gain;
prioritize improvements that reduce first-use work itself. Continue with
cold event/draw/raster spikes, mixed/metric-changing
preparation, cross-frame grouping and sparse inverse clips. Full-output validity
needs a focused reproduction/fix of the wrapper invalidation overwrite before
longer reuse. Fractional-key canonicalization requires a defined precision
contract. Raster byte/owner budgets remain separate from event admission.
Possible next experiments are compact immutable tag/parameter storage to reduce
cold heap traffic, and syntax-only prewarming for events whose completed state
cannot be retained. Both need legacy numeric/ownership and packet-lead gates.
TextInfoCacheKey already excludes rotation/shear; verify current source before
pursuing that older suspicion. Keep one CPU at Above Normal and no playback.

## Previous event/layout follow-up

The user selected item 1 with a generalized approach. See
[general event preparation and layout reuse](performance-20261027-event-layout.md).
The candidate retains uniform-span layout and static event state, reevaluates
layout-independent paint operations with legacy arithmetic, and uses bounded
immutable polygon paths and last-appearance words. Repeated x64 comparisons
show 6.08% lower median CPU and 2.72% lower burst render time. Win32 median CPU
is 2.26% lower, but burst time is 0.78% higher. All x86/x64 DLLs are compiled;
22 contracts, 54 fixtures, 13 extended scripts, full sample at 256/zero cache
entries, composition and direct cross-architecture pixel gates pass with zero
mismatches. The 200-event synthetic parse workload and 15 warm pixel probes
also complete. Largest spikes remain essentially unchanged.
No title/time/style-name heuristics, untimed prewarming or playback are used.

Next item-1 opportunities include mixed styled runs, metric-changing properties
and bounded preparation before activation with all startup/earlier-frame work
measured. Synthetic rotated/sheared text also warrants a focused review of
TextInfoCacheKey's metric dependencies. No claim is made that the entire item
is implemented or that frame-time spikes have been eliminated.

## Previous frame grouping follow-up

See [the frame-grouping scratch report](performance-20261027-grouping-scratch.md).
Grouping now uses contiguous item metadata, inline initial memberships and
a frame-owned stable pool for merged membership headers. All x86/x64 DLLs and
console builds are compiled and pass the 21 contracts, 54 fixtures, 12 extended
scripts, full sample at 256/zero cache entries, composition replay and direct
cross-architecture pixel comparison with zero mismatches. Median full-render
CPU is 3.01% lower on x64 and 3.40% lower on Win32; x64 overall batches vary,
while composition-only CPU improves 5.82%. Budget exceedances remain, so zero
dropped playback frames is not established. No playback was run in this batch.

The fresh console profile still puts substantial time in packed single-color
blending and alpha-mask work. That is the next isolated kernel candidate, with
packed/planar and alpha-convention differential gates first. Active ordering
and expiry, preparation/layout dependencies and cross-frame grouping reuse
remain pending. Keep startup work and retained bytes visible when measuring.

## Previous font/style and layout follow-up

See [the font/style and layout follow-up](performance-20261027-font-layout.md).
GetSubtitle now has a lightweight cached-state path with call-scoped Unicode
borrowing and existing ANSI conversion. CreateSubtitle directly initializes
styles; one non-whitespace/non-break word skips general wrapping/compaction.
The existing cache budgets and invalidation rules are retained.

Only console checks on one render thread/logical CPU at Above Normal priority
are authorized by the current request. No playback or automation was started
for this batch. The earlier one-pass native/libass playback comparison is a
separate completed experiment.

The current profile ties most CloneData callers under GetSubtitle to source
string copy/refcount work. CString Trim already avoids cloning unchanged font
names; do not assume a no-op font-trim change will remove those samples.

Next work should reprofile the resulting first-use path before adding broader
layout/style reuse. Conservative event preparation must measure startup or
earlier-frame work and retained bytes rather than hide work outside the timed
window. Preserve exact animation timestamps and both runtime paths. Grouping
metadata, composition kernels, active ordering/expiry and dependency-based
layout reuse remain separate pending approaches. Keep all x86/x64 pixel,
zero-cache, inverse-clip, large-event and retained-frame gates.

## Offline console work reauthorized, 2026-10-26

The user subsequently requested production libass support and serial console
benchmarks restricted to one logical CPU at Above Normal priority. This
supersedes the code-only restriction for builds and offline checks. Zoom Player
playback/profiling remains paused. See [libass integration](libass-integration.md)
and the benchmark README for the new entry points.

## Earlier code-only checkpoint, 2026-10-26

The user is using the PC and authorizes code/documentation work only. Do not
build, execute tests, launch players, profile, run benchmarks or analyze traces
until the user makes the computer available again. No automation was created.

The compact membership/eight-inline-ID candidate was already compiled for both
architectures and passed 17 contracts, 54 fixtures, the complete sample at
256/zero bitmap entries and eight extended scripts. It is preserved in
`benchmark-results/next-20261026-compact-final-<arch>` with matching source and
symbol snapshots. Session 86 ended early, is excluded, and was stopped with code
0; the owned player/helper closed and temporary settings were restored.

Subsequent group-key-view code and an 18th contract are written but unbuilt and
untested. They eliminate the temporary child-key vector for synchronous lookup
while materializing ownership for cached copies. These newer workspace sources
are not the validated compact binaries. See the [current checkpoint](performance-20261026-compact-grouping.md)
for exact boundaries, pending gates and the unrun playback sequence.

## Completed validation 2026-10-26

The first localized review batch is compiled for x86 and x64. All 15 contracts,
54 fixtures, all 2,428 sample frames at 256/zero bitmap capacity, and all seven
extended scripts pass with zero pixel mismatches on both architectures. Eight
Debug MAX Zoom Player captures completed. Observed mean CPU fell 4.13% on x86
and 0.72% on x64, but drop ranges overlap (controls 48/39 and 49/45; candidates
44/39 and 45/44). Zero drops is not achieved. All test processes are closed and
temporary player/COM/cache settings are restored. See the
[results and next steps](performance-20261026-review-implementation.md).

Next: profile the actual x64 helper, measure phase/allocation costs, and target
active-set ordering/expiry or remaining frame/group construction. Preserve
streaming invalidation and pixel gates. The stopped/paused snapshots below are
historical; the published v3.2.1.900 release remains unchanged.

## Paused 2026-10-25

The user explicitly paused optimization/testing. Do not resume builds, native tests, profiling or playback until requested. The manual Zoom Player closures are explained; the exit investigation was removed. The Z: drive is currently unavailable, which must be resolved before the next playback/profile capture. No replacement player path or drive mapping was created.

Both x86 and x64 grouping candidates are compiled with matching DLLs/PDBs/harnesses under `benchmark-results/resume-20261025-grouped-Win32` and `resume-20261025-grouped-x64`. They pass 13 focused contracts, 54 fixture cases, all 2,428 sample frames, and six extended regression scripts. The first x64 candidate playback recorded 40 drops / 48.78125 CPU seconds versus the control's 53 / 46.640625; the higher CPU means improvement is not yet established. The published v3.2.1.900 release remains unchanged.

Next: restore access to `Z:\zpAI`, repeat/isolate grouped draw storage versus borrowed-key lookup, complete x86 and 1,024-entry x64 playback captures, and profile the actual x64 helper. Session 85 never started; the most recent attempt failed before player launch or any temporary settings/COM changes. See [current results](performance-20261025-x64-grouping.md).

## Resumed 2026-10-25

The user requested continued optimization and x64 testing. The saved final CPU trace and six deferred x86 scripts have now been processed. See the [current x64/grouping report](performance-20261025-x64-grouping.md); the stopped-state snapshot below is historical.

## Release update

The user subsequently requested publishing **v3.2.1.900**. All three DLLs were rebuilt with the new version; the renderer sources remain identical to the measured candidate. Playback testing and pending investigations remain paused. See [release notes](releases/v3.2.1.900.md) and [archived measurement summary](benchmarks/v3.2.1.900.json).

## Stopped state

Optimization, playback testing, native regression runs and profiling are stopped at the user's request. Resume only when the user makes the PC available again. No scheduled follow-up was created.

The latest stable candidate is already compiled in `bin/lib_18.0/Win32/Release/`: `XySubFilter.dll`, `VSFilter.dll`, and `xy_sub_filter_consumer.dll`. All three targets built successfully with Win32 Release / v145 (61 existing warnings, zero errors). The benchmark player remains a separate Debug (`LOCAL`) MAX build of `Z:\zpAI\zplayer.exe`.

The frozen XySubFilter DLL, matching PDB, source snapshots and native harness are in `benchmark-results/word-keys-exact/`. DLL SHA256: `b87fff108872c7193d64748e9dbc17bd7a4436e909cbe6416b8d289cc02f75c0`. The checked-out modified subtitle sources match the compiled snapshot. See [build manifest](../benchmark-results/word-keys-exact/build-manifest.json).

## Completed evidence

The sample's complete 2,428-frame subtitle bitmap output matches the original renderer exactly at both 64 and 256 bitmap-cache entries, including 2,184 nonempty frames. Twelve focused ownership/cache/behavior contracts and the 54-case fixture comparison passed before stopping. The six additional scripts listed below have not yet been rerun against the final key-retention build.

The last completed ordinary playback run recorded zero new drops from 22:20 through EOF. All six final captures below recorded zero drops after the initial polling interval. Four captures recorded one new drop in that first interval; two ordinary 256-entry runs recorded zero for the whole interval. Subtitles start at 22:20.360, so the cause of the initial drop is unresolved. Full-window zero drops are not yet guaranteed for every fresh-process run.

| Capture | Bitmap entries | New drops, full interval | CPU seconds | Peak private MiB |
| --- | ---: | ---: | ---: | ---: |
| `word-keys-exact-unprofiled` | 64 | 1 | 62.4375 | 603.0 |
| `word-keys-exact-unprofiled-02` | 64 | 1 | 58.2656 | 595.7 |
| `word-keys-exact-bitmap256` | 256 | 1 | 59.2500 | 597.6 |
| `word-keys-exact-bitmap256-02` | 256 | 0 | 61.9844 | 597.9 |
| `word-keys-exact-bitmap256-03` | 256 | 0 | 61.4844 | 601.8 |
| `word-keys-exact-bitmap256-profiled` | 256 | 1 | 59.0156 | 601.2 |

The fresh control recorded 599 drops and 77.5625 CPU seconds. The five final unprofiled runs used 58.27-62.44 CPU seconds (about 22% lower on average). The 64/256 CPU results overlap; larger cache capacity is not proven faster. The 256-entry native experiment consumed about 10.3 MiB more peak commit and increased bitmap/group-key hits from 1.9% to 44.8%. Source defaults remain 64, and the temporary registry override was restored to 64. The user authorized reasonable extra memory, but a permanent capacity change needs evidence from broader workloads.

Raw results: [saved playback summary](../benchmark-results/word-keys-final-saved-results.json). Detailed implementation and earlier experiments: [reconstruction report](performance-20261023-reconstruction.md).

## Next steps, in order

1. **Analyze the already captured final CPU trace.** No new playback is needed for this step. Session 84 was stopped successfully; `benchmark-results/word-keys-exact-bitmap256-profiled/cpu.diagsession` is saved. Run `test/benchmark/analyze_cpu_trace.ps1` with that folder, symbols `benchmark-results/word-keys-exact`, elapsed 25-65 seconds. Verify lost-event count and sampled XySubFilter module paths before comparing to the text-span and polygon-interval profiles. Preserve functions/modules/stacks CSVs. Interpret inclusive samples as overlapping and account for optimized x86 inline frames.
2. **Complete final additional regression coverage at 256 entries.** Use the preserved final harness and fonts, with existing controls; do not rerender controls. Scripts: `reconstruction.ass`, `zero-alpha.ass`, `polygon-font-fields.ass`, `geometry-reuse.ass`, `multiline-inverse.ass`, `omitted-numbers.ass` under the benchmark media folder. Controls are respectively `geometry-reuse/reconstruction-fixture.csv`, `zero-alpha-fixture.csv`, `polygon-font-fixture.csv`, `geometry-validation-control.csv`, `multiline-validation-control.csv`, and `omitted-validation-control.csv` under `benchmark-results/`. These passed the earlier interval candidate; the final per-word key retention still needs this extended coverage, especially inverse clipping and transparent overlays.
3. **Investigate the occasional first-second drop.** Give the Zoom Player developer [the diagnostic handoff](zoom-player-startup-drop-handoff.md). Capture coherent QPC seek/pause/run and renderer statistics without changing the existing TCP 1150 response. Compare identical polling cadences and graph/window/track settings; the separate 50 ms startup diagnostic must not be pooled with standard one-second captures. Establish whether this is source delivery, presentation, startup scheduling or counter reporting before changing code.
4. **Review the memory preset.** The current retained reconstruction cache has 8,192 entries and a 32 MiB accounted limit; its native peak was 5,918 entries / 26,157,056 accounted bytes, with zero retained states at EOF. Accounting is distinct from process private bytes. The event-object pool has a separate 1 MiB storage budget. Test longer seeks/track changes/edits before broadening cache admission. Compare 64 and 256 bitmap entries only after controlling variance; increasing to 512/1024 is low priority because the final ending already played without drops and CPU results overlap.
5. **Choose further code work from the final profile.** Candidate areas are remaining draw-item allocation, grouping/key bookkeeping and alpha blending. Review exact ownership, invalidation, hash/equality and clip semantics before introducing another pool or cache. Overlay and raw-path caches already hit about 99.94%/99.97%, so growing them is unlikely to help much. Any retained object should have a clear bounded budget, cache invalidation policy and pixel-equivalence gate.
6. **Only if new code changes are justified**, rerun the same Debug Zoom Player benchmark from 22:20 through EOF with the default track, same renderer and window, including startup drops in full-window totals. Recheck all 2,428 bitmap hashes, focused contracts and additional scripts. Keep CPU-heavy analysis/builds/native tests separate from playback. Freeze matching DLL/PDB/source snapshots and update the knowledge graph/report.

## Cleanup at stop

The benchmark-owned Debug player and CPU sampler are closed. The unrelated installed Zoom Player was left alone. The 32-bit HKCU COM override is absent and the bitmap capacity is restored to 64. The wrapper records completion-action restoration and successful cleanup in both final capture setup JSON files. The installed Program Files filter was not replaced. No additional tests or profile analysis were run after the user's stop request; the only remaining activity was reading saved results and recording this handoff.
