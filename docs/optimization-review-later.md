# Optimization work for later review

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
