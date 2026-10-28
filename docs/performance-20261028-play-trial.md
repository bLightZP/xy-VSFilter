# Native renderer playback trial — 2026-10-28

The latest validated **3.2.1.900** native renderer completed one measured playback trial per architecture with **2 renderer-dropped frames on Win32 and 2 on x64**. Neither run dropped frames during its first two seconds. This is close to the zero-drop target, but the remaining drops occurred later in playback and cannot be dismissed as pause/resume startup behavior.

## Conditions

- Player: **Debug / LOCAL / MAX** `Z:\zpAI\zplayer.exe`, freshly rebuilt with its existing `Build.ps1 -Configuration Debug -Edition MAX`; both helpers were rebuilt too. Executable hashes and helper PE architecture were verified against the successful Debug build record.
- Win32 used the internal graph. x64 used the native external64 helper; the main Zoom Player application remains 32-bit. Expected workspace XySubFilter DLLs were verified loaded inside the corresponding graph processes, with hashes matching the latest tag-storage stable snapshots.
- Media: `Subtitles_CPU_killer_on_end_credits.mkv`, **22:20 through EOF at approximately 24:01.272**, default ASS track **0 / Signs & Songs [eng]**, explicitly enabled. Native rendering was selected; libass was not part of this trial.
- Renderer: **MPC Video Renderer**. Both runs used the same visible, unminimized player window, client size **2084 × 1176**.
- Bitmap cache: **256 items**, matching the preceding optimization workload; the pre-existing installed setting was 64 and was restored afterwards. Results therefore describe the 256-item configuration.
- Measurements: TCP **1150**, approximately one-second polls; independent process-time sampling includes Zoom Player and descendant media helpers. CPU percentages use all **8 logical processors** as the denominator and include decoding, subtitle rendering, video presentation and Debug logging. These are whole-playback measurements, not isolated subtitle CPU.
- Playback had normal unrestricted CPU affinity. No offline render benchmark, profiler, compiler or pixel-comparison workload overlapped the captures.

## Results

| Metric | Win32 / internal32 | x64 / external64 |
| --- | ---: | ---: |
| Renderer-dropped frames | **2** | **2** |
| Drops during first two seconds | **0** | **0** |
| Average whole-machine CPU | **4.79%** | **5.16%** |
| Average logical-core equivalents | 0.384 | 0.413 |
| Total process-tree CPU time | 39.125 s | 42.141 s |
| Capture wall time | 102.003 s | 102.002 s |
| Completion | EOF | EOF |

The dropped counter began at zero, ended at two, and remained monotonic in each capture. Both renderers reset only their drawn counter during the first second; the existing summary validator accepts this specific startup reset because the dropped counter remains independently valid. Drawn counters are not used to calculate a dropped-frame percentage.

| Architecture | Interval containing first drop | Interval containing second drop |
| --- | --- | --- |
| Win32 | 22:46.958–22:47.958 | 23:17.959–23:18.957 |
| x64 | 22:46.827–22:47.849 | 23:02.810–23:03.811 |

These are polling intervals, not exact frame timestamps. They overlap dense subtitle activation periods identified in the offline work; this trial does not independently attribute the drops to a particular renderer function.

## Setup and cleanup

An initial x64 setup attempt was rejected before measurement because the saved audio-visualization flag caused Zoom Player to choose its internal graph. Its artifacts are retained separately as `x64-setup-failed-01` and excluded from the results. Temporarily disabling **OPEnableVis** allowed the requested external64 graph; the flag was restored afterwards. Video visualization was already disabled. There was exactly one completed measured capture per architecture and no repetition to select a better result.

The benchmark-owned player and helpers were closed. The completion action, engine-routing settings, audio-visualization flag, native/libass preference, per-user COM registrations and bitmap-cache settings were restored and checked. Installed filter files were not replaced. No software was installed, and no subtitle implementation was changed for this trial.

## Interpretation and evidence

Both architectures are now close to the playback target in this sample/configuration, with two remaining drops over the animated ending. Low average CPU does not rule out short deadline misses. A single run per architecture is a current-state snapshot; it does not establish repeatability or an x86/x64 performance ranking. Historical captures used different player builds and window geometry, so these numbers are not a controlled optimization speedup comparison.

The preceding native change and its pixel-equivalence evidence are recorded in [the tag-storage report](performance-20261028-tag-storage.md). No new pixel comparison was needed for this playback-only request.

- [Portable results and provenance](benchmarks/20261028-play-trial.json)
- Raw captures: `benchmark-results/play-trial-20261028/Win32/` and `benchmark-results/play-trial-20261028/x64/` (`metadata.json`, `renderer.csv`, `cpu.csv`, TCP and player logs).
- Aggregation and cleanup: `benchmark-results/play-trial-20261028/results.json`, `Win32-summary.json`, `x64-summary.json`, and the per-architecture native-setting/setup records.
- Debug build record: `benchmark-results/play-trial-20261028/zoom-debug-build/MAX/Debug-summary.json`.

Raw captures/build logs are ignored local artifacts; the portable JSON preserves both validated summaries, drop intervals, dependency hashes and restoration evidence.
