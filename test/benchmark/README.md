# Zoom Player renderer benchmark

Use a **Debug (`LOCAL`) build of `Z:\zpAI\zplayer.exe` and both helpers**, with the sample's default subtitle track. Unsigned Release builds cannot be used for these tests. The stress workload is 22:20 through the end of `F:\Git\xySubFilter\Subtitles_CPU_killer_on_end_credits.mkv`. Keep the player settings and active filter graph consistent between baseline and candidate runs.

For an automated capture including CPU data, first obtain a successful Debug build record using Zoom Player's existing `Build.ps1 -Configuration Debug -Edition MAX`. Then run:

```powershell
python F:\Git\xySubFilter\test\benchmark\run_playback_baseline.py --build-record F:\Git\xySubFilter\benchmark-results\zoom-debug-build\MAX\Debug-summary.json --output F:\Git\xySubFilter\benchmark-results\baseline-next
```

The runner verifies that the player matches the Debug build record, launches a separate player instance on TCP port 33001, clears that instance's playlist, loads the sample, pauses, enables the flagged default subtitle (track 0), seeks to 22:20, and resumes. It disables automatic replay with `exPlayComplete,0` to preserve EOF counters and leaves the player available for inspection. `--attach-pid` can instead use an existing instance on the selected port; its playlist will also be replaced by the sample. Commands ignored during graph loading are retried before measurement.

`renderer.csv` records independent 1150 samples. `cpu.csv` uses Windows `GetProcessTimes` about once per second, independently of TCP response timing, for the player and any descendant media helpers. CPU percentages are normalized over all logical CPUs; `cpu_cores` records logical CPU equivalents. CPU covers the entire playback process, including decoder, subtitle rendering, renderer, and Debug logging. Metadata includes the machine, executable/build hashes, selected subtitle, registered filter DLL hashes, and completion status. Partial or failed attempts remain distinguishable from completed EOF captures.

The collector also records private and working-set bytes with `K32GetProcessMemoryInfo`. These cover the whole process, including the video renderer and decoder.

For repeated DLL experiments, `benchmark_filter.py --dll <workspace-DLL> --output <new-folder>` temporarily sets the 32-bit per-user COM registration, runs the same benchmark, closes its player, restores the completion action, and removes/restores that registration. The installed DLL remains untouched. Optional `--profile-session <unused-number>` starts/stops the installed Visual Studio sampler around the capture. Run with permission to collect ETW data and restore the player's settings file. It refuses to reuse an occupied benchmark TCP port. `--startup-poll-interval 0.05` uses the same TCP connection for 50 ms renderer sampling during the first two seconds, then returns to one-second polling. This is a separate startup diagnostic; ordinary comparisons keep the default interval of one second. Metadata records the interval and visible benchmark window geometry. `--bitmap-cache-items 256` temporarily changes `HKCU\Software\Gabest\XySubFilter\Performance\bitmap_mru_cache_item_num` and restores its original value after closing the benchmark player; setup/metadata record the override and restoration. This intentionally changes the cache capacity while preserving player, media, track, renderer and seek conditions.

The completed 2026-10-23 capture is in `benchmark-results/baseline-20261023-05`: [report](../../benchmark-results/baseline-20261023-05/report.md), [renderer CSV](../../benchmark-results/baseline-20261023-05/renderer.csv), and [CPU CSV](../../benchmark-results/baseline-20261023-05/cpu.csv). These generated artifacts are ignored by Git.

For manual playback control with renderer-only collection, start a Debug benchmark instance with its own TCP port:

```powershell
& 'Z:\zpAI\zplayer.exe' '/MultInst' '/TCP:33001' '/Seek:00:22:20' 'F:\Git\xySubFilter\Subtitles_CPU_killer_on_end_credits.mkv'
```

Then collect for 110 seconds using the installed Python and a new output filename:

```powershell
python F:\Git\xySubFilter\test\benchmark\collect_renderer_stats.py --port 33001 --seconds 110 --output F:\Git\xySubFilter\renderer-baseline.csv
```

The collector only queries the player. It does not load media, seek, select tracks, or change settings. Launching and seeking before starting collection excludes initial graph startup drops from the measured deltas; compare the same interval in every run. Keep the renderer visible during performance measurements.

TCP request `1150\r\n` returns:

```text
1150 PositionMS|RendererName|FramesDrawn|FramesDropped|AverageFPS
```

There are exactly five fields. Position is in milliseconds; AverageFPS is already `IQualProp.get_AvgFrameRate / 100`, so the collector does not divide it again. Empty fields stay empty in CSV, and valid zeroes stay zero. Renderer names are supplied with pipe replaced by slash and CR/LF/NUL replaced by spaces. External engines report their latest helper snapshot, so adjacent polls can repeat a sample.

CSV includes cumulative counters, deltas between adjacent replies, UTC and elapsed times, and query latency. A renderer change, backwards position, or decreasing counter sets `counter_reset=1` and leaves that row's deltas empty. A first sample or unavailable counter also has an empty delta. Query errors fail the command and leave a partial CSV; they do not become zeroes. An existing output file is never overwritten.

Sum `delta_frames_dropped` within a continuous measured interval, checking that counters are available and that the interval contains no resets. These are renderer statistics, not a count of frames dropped elsewhere in the graph. `average_fps` is the renderer's average, not an instantaneous per-second measurement. No performance claim follows from an empty counter.

FFmpeg/FFprobe are available in `F:\Git\Clip Chopper\ffmpeg` for inspecting the sample. Do not install new software for testing.

## Native CPU sampling

The installed Visual Studio 2026 Community includes `Team Tools\DiagnosticsHub\Collector\VSDiagnostics.exe`. Run from that directory, with permission to collect ETW data:

```powershell
.\VSDiagnostics.exe start 73 /attach:<player-pid> /loadConfig:AgentConfigs\CpuUsageLow.json
.\VSDiagnostics.exe stop 73 /output:F:\Git\xySubFilter\benchmark-results\<run>\cpu.diagsession
```

Keep the matching DLL and PDB together for each build. Analyze with the installed TraceEvent library:

```powershell
F:\Git\xySubFilter\test\benchmark\analyze_cpu_trace.ps1 -Folder F:\Git\xySubFilter\benchmark-results\<run> -Symbols F:\Git\xySubFilter\benchmark-results\<build>
```

The analysis filters samples to the player PID and capture timestamps, and writes function and module CSVs plus sample/stack/lost-event counts. `cpu-module-paths.csv` distinguishes loaded DLLs with the same basename. Optional `-StartElapsed 20 -EndElapsed 100` restricts analysis to a common window within each capture. Optimized x86 stacks can omit inlined callers; inclusive counts are not an exhaustive phase timing breakdown. Use unprofiled captures for the final playback comparison:

```powershell
python F:\Git\xySubFilter\test\benchmark\compare_playback.py F:\Git\xySubFilter\benchmark-results\control-unprofiled F:\Git\xySubFilter\benchmark-results\candidate-unprofiled --output F:\Git\xySubFilter\benchmark-results\comparison.json
```

## Source builds and pixel equivalence

The installed toolchain is VS 2026 Community (`v145`). `tools\yasm.exe` was supplied by the user and is ignored by Git. Append that directory to the build process's PATH. From a Visual Studio Developer PowerShell, build the unmodified control before changing headers, and retain its DLL, PDB, and validation executable:

```powershell
MSBuild VSFilter.sln /t:xy_sub_filter /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v145 /m
MSBuild test\benchmark\render_subtitles.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v145 /p:SolutionDir=F:\Git\xySubFilter\
```

XySubFilter is built optimized with symbols. **The Debug requirement applies to Zoom Player and its helpers.** This keeps playback authorized while measuring the subtitle filter's normal optimized code.

The separate `render_subtitles` target links the shared renderer libraries. Extract the default ASS stream (`-map 0:3 -c:s copy`) and all attached TTF/OTF fonts with FFmpeg into an ignored directory. Use explicit output filenames for attachments, since FFmpeg rejects some embedded filenames as unsafe. Fonts are loaded privately into the validation process with `FR_PRIVATE`; they are not installed.

```powershell
render_subtitles.exe --hash-contract
render_subtitles.exe --cache-contract
render_subtitles.exe --intern-contract
render_subtitles.exe --geometry-contract
render_subtitles.exe --inline-list-contract
render_subtitles.exe --alpha-buffer-contract
render_subtitles.exe --zero-alpha-contract
render_subtitles.exe --effect-contract
render_subtitles.exe --reconstruction-pool-contract
render_subtitles.exe --layer-key-contract
render_subtitles.exe --reconstruction-cache-contract
render_subtitles.exe --word-key-cache-contract
render_subtitles.exe --fixtures <new-fixture.csv>
render_subtitles.exe default.ass <font-directory> <new-output.csv>
```

The render check evaluates all 2,428 source frame midpoints within 22:20–EOF at 24000/1001 fps, at 1920x1080. Compare `(frame, time_100ns, bitmaps, sha256)` between control and candidate. SHA256 covers each bitmap's position, size, and visible RGBA bytes, excluding cache identities and pitch padding. This validates rendered output independently of decoder/renderer drops. Offline `render_ms` is wall time and is sensitive to other activity; use it for diagnosis, with the playback CPU CSV for end-to-end measurements.

`--cache-contract` compares cached and bypassed renders across animation, expiry, backward seeking, text edits, malformed tags, a tiny cache budget, and stream invalidation. `--fixtures` emits 54 small rendering cases for comparison with a preserved control executable, covering numeric, transform, clip, karaoke, drawing, and malformed-tag behavior. The normal render prints event-cache hits/misses, current retention and peak accounted retention. The admission accounting conservatively charges shared data to each owner; it is distinct from Windows private-byte measurements.

`--intern-contract` checks string identity, owned storage after caller mutation, and live-value survival after cache eviction. The ASS files under `fixtures/` exercise inverse rectangular clipping with multiple lines and fractional polygon coordinates, plus omitted numeric arguments. Render each with both preserved executables using the normal three-argument invocation, then compare their hashes. These fixtures use the same 22:20–EOF timestamps as the sample.

`--geometry-contract` compares cold and warm polygon vertices and metrics, exercises an actual path-cache hit, checks exact scale matching and owned buffers after eviction, and rejects a simulated text-path collision for zero script scales. `fixtures/geometry-reuse.ass` adds animated drawing scale, rotation and baseline coverage against an unmodified renderer.

`--inline-list-contract` checks spill order, truncation, refill, shared ownership, line iteration and repeated subtitle clearing. `--alpha-buffer-contract` checks mask-buffer growth, alignment and release, and compares reused/fresh mask pixels across clipping, body/border selection, color switching and vector masks. `fixtures/reconstruction.ass` covers mixed HTML/ASS, malformed delimiters, consecutive tags, whitespace, line wrapping and drawing/text transitions against a preserved executable.

`--zero-alpha-contract` verifies that a zero mask preserves background pixels in packed, planar and additive paths. `--effect-contract` checks independent effects, default state and repeated clearing. `--reconstruction-pool-contract` checks reuse, content release, default state, clearing and the 1 MiB storage bound. `--layer-key-contract` checks shared geometry, fractional and negative positions, opaque-box isolation and ownership. `--reconstruction-cache-contract` compares reuse against bypassed reconstruction across exact step boundaries, nested transforms, continuous interpolation, movement, fading, seeking, edits, tiny limits, byte accounting, expiry and invalidation for both polygon and text states. `--word-key-cache-contract` checks fixed-size key retention, eviction, changed text/style/colors/positions/clippers/flags, near-equal geometry hashes, and ownership after release. The normal render prints reconstruction hits/admission counts, pool reuse, MRU counters and native memory counters. To compare bitmap capacities offline, use `render_subtitles.exe --bitmap-cache-items 256 default.ass <fonts> <new.csv>`; bitmap identities remain excluded from pixel comparisons. `fixtures/zero-alpha.ass` checks transparent layers over an existing background; `fixtures/polygon-font-fields.ass` checks font-independent drawing geometry together with font-dependent text.

CPU analysis also exports `cpu-stacks.csv`, with complete sampled call chains and counts. Counts are CPU samples, not allocation counts.

```powershell
python test\benchmark\compare_rendered.py <control-render.csv> <candidate-render.csv> --output benchmark-results\pixel-comparison.json
```
