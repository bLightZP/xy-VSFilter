# Zoom Player renderer-startup diagnostic handoff

## Objective

Explain and remove the occasional first-second renderer drop after resuming the paused 22:20 seek, while keeping the optimized animated ending drop-free. This is a follow-up to [the XySubFilter reconstruction benchmark](performance-20261023-reconstruction.md).

The optimized filter has produced a complete zero-drop 22:20-EOF run with ordinary one-second polling. Other completed runs recorded one drop in the first polling interval and none during the ending. A separate 50 ms startup-polling diagnostic was also entirely drop-free. These observations do not establish the cause of the initial drop. Subtitles first appear at approximately 22:20.360, so the initial interval includes subtitle work.

## Fixed test conditions

- Run a successful Debug (`LOCAL`) MAX build of `Z:\zpAI\zplayer.exe` and its helpers. Record binary hashes using the existing build summary.
- Use `F:\Git\xySubFilter\Subtitles_CPU_killer_on_end_credits.mkv`, flagged default track 0 (`Signs & Songs [eng] (ass) [default]`), seek 1,340 seconds, then play through EOF at 1,441.272 seconds.
- Preserve LAV splitter/decoder, XySubFilter and madVR graph topology, renderer settings and window geometry. The recorded main client size is 970x550, with approximately 960x540 video.
- Use the frozen optimized DLL and matching PDB in `F:\Git\xySubFilter\benchmark-results\word-keys-exact`. Its DLL SHA256 is `b87fff108872c7193d64748e9dbc17bd7a4436e909cbe6416b8d289cc02f75c0`.
- The benchmark wrapper can temporarily use 256 bitmap/group-key entries and restores its original registry value. Keep the chosen capacity consistent within each comparison.
- Use installed tools; new software installation requires the human user to supply it.

## Required instrumentation

1. Add a Debug trace with QueryPerformanceCounter timestamps and graph-generation identifiers for load completion, pause completion, seek request/completion, run request/return and the first renderer-statistics samples. Reuse existing graph notification callbacks for additional timing evidence.
2. Capture the raw `IQualProp` drawn/dropped/average-frame-rate values together with media position and graph state. Distinguish unavailable values from valid zeroes. Mark counter resets or graph/renderer replacement explicitly.
3. Inspect the `1150` handler for scheduling dependencies. Keep statistics sampling read-only and report its collection timestamp, query duration, thread and data age. Compare one-second polling with 50 ms polling for the first two seconds to determine whether observation changes startup timing.
4. Preserve the existing five-field `1150` response contract. Put richer diagnostics in a separately allocated unused API message or Debug trace file, with a documented schema and decimal-point formatting.
5. Where renderer interfaces already expose source-sample and presentation/refresh counters separately, capture both with their definitions. Current madVR `FramesDrawn`/average FPS reflect approximately 120 Hz presentation, rather than the 23.976 fps source-frame count.
6. Record any startup readiness or queue changes proposed by the investigation. Keep initial counters, first-second increments and later playback drops separate in reports; include all increments in the full-window result.

## Validation and delivery

Use `test\benchmark\benchmark_filter.py` for the ordinary benchmark. `--startup-poll-interval 0.05` is a separate diagnostic mode. The wrapper verifies Debug binaries, enables the default track and restores temporary settings/registration.

Compare repeated fresh-process runs with the same polling cadence, graph, selected track and window. Show timestamps bracketing any initial counter increase and explain whether it represents a lost source frame, a presentation/refresh drop or a counter/reporting issue. Verify the final fix with complete 22:20-EOF captures, including the first second, and with zero drops during the animated ending. Deliver source changes, a successful Debug build record, raw renderer/CPU traces and a concise explanation supported by the new timestamps.
