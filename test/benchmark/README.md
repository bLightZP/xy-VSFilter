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
render_subtitles.exe --group-key-contract
render_subtitles.exe --cache-capacity-contract
render_subtitles.exe --karaoke-hash-contract
render_subtitles.exe --fixtures <new-fixture.csv>
render_subtitles.exe default.ass <font-directory> <new-output.csv>
```

The render check evaluates all 2,428 source frame midpoints within 22:20Ã¢â‚¬â€œEOF at 24000/1001 fps, at 1920x1080. Compare `(frame, time_100ns, bitmaps, sha256)` between control and candidate. SHA256 covers each bitmap's position, size, and visible RGBA bytes, excluding cache identities and pitch padding. This validates rendered output independently of decoder/renderer drops. Offline `render_ms` is wall time and is sensitive to other activity; use it for diagnosis, with the playback CPU CSV for end-to-end measurements.

`--cache-contract` compares cached and bypassed renders across animation, expiry, backward seeking, text edits, malformed tags, a tiny cache budget, and stream invalidation. `--fixtures` emits 54 small rendering cases for comparison with a preserved control executable, covering numeric, transform, clip, karaoke, drawing, and malformed-tag behavior. The normal render prints event-cache hits/misses, current retention and peak accounted retention. The admission accounting conservatively charges shared data to each owner; it is distinct from Windows private-byte measurements.

`--intern-contract` checks string identity, owned storage after caller mutation, and live-value survival after cache eviction. The ASS files under `fixtures/` exercise inverse rectangular clipping with multiple lines and fractional polygon coordinates, plus omitted numeric arguments. Render each with both preserved executables using the normal three-argument invocation, then compare their hashes. These fixtures use the same 22:20Ã¢â‚¬â€œEOF timestamps as the sample.

`--geometry-contract` compares cold and warm polygon vertices and metrics, exercises an actual path-cache hit, checks exact scale matching and owned buffers after eviction, and rejects a simulated text-path collision for zero script scales. `fixtures/geometry-reuse.ass` adds animated drawing scale, rotation and baseline coverage against an unmodified renderer.

`--inline-list-contract` checks spill order, truncation, refill, shared ownership, line iteration and repeated subtitle clearing. `--alpha-buffer-contract` checks mask-buffer growth, alignment and release, and compares reused/fresh mask pixels across clipping, body/border selection, color switching and vector masks. `fixtures/reconstruction.ass` covers mixed HTML/ASS, malformed delimiters, consecutive tags, whitespace, line wrapping and drawing/text transitions against a preserved executable.

`--zero-alpha-contract` verifies that a zero mask preserves background pixels in packed, planar and additive paths. `--effect-contract` checks independent effects, default state and repeated clearing. `--reconstruction-pool-contract` checks reuse, content release, default state, clearing and the 1 MiB storage bound. `--layer-key-contract` checks shared geometry, fractional and negative positions, opaque-box isolation and ownership. `--reconstruction-cache-contract` compares reuse against bypassed reconstruction across exact step boundaries, nested transforms, continuous interpolation, movement, fading, seeking, edits, tiny limits, byte accounting, expiry and invalidation for both polygon and text states. `--word-key-cache-contract` checks fixed-size key retention, eviction, changed text/style/colors/positions/clippers/flags, near-equal geometry hashes, and ownership after release. The normal render prints reconstruction hits/admission counts, pool reuse, MRU counters and native memory counters. To compare bitmap capacities offline, use `render_subtitles.exe --bitmap-cache-items 256 default.ass <fonts> <new.csv>`; bitmap identities remain excluded from pixel comparisons. `fixtures/zero-alpha.ass` checks transparent layers over an existing background; `fixtures/polygon-font-fields.ass` checks font-independent drawing geometry together with font-dependent text.

CPU analysis also exports `cpu-stacks.csv`, with complete sampled call chains and counts. Counts are CPU samples, not allocation counts.

`--cache-capacity-contract` checks zero-capacity bypass, enabling/shrinking and
disabling caches, owned-key survival, fresh bypass identities and coupled bitmap
capacities. `--karaoke-hash-contract` checks all six switch words and 1,000
distinct progressing split positions. `--bitmap-cache-items 0` is an explicit
cache bypass. Compare it against an enabled-cache pixel control, including
retained frames across runtime changes. `fixtures/edge-blur-karaoke.ass` covers
integer/fractional and mixed Gaussian/edge blur, tiny drawings, wide outlines,
scale/rotation changes, clipping and progressing karaoke against a preserved
pre-change executable. See the [implementation checkpoint](../../docs/performance-20261026-review-implementation.md)
for recorded builds, pixel comparisons and playback results.

```powershell
python test\benchmark\compare_rendered.py <control-render.csv> <candidate-render.csv> --output benchmark-results\pixel-comparison.json
```

## x64 and resumed benchmarks

Build `render_subtitles.vcxproj` with `/p:Platform=x64` for the native x64 gates. The DLL solution already has x64 targets. Keep PDBs and native executables with their matching DLL build.

`benchmark_filter.py --engine external64 --dll <x64-DLL> --build-record <current-Debug-summary.json>` uses the player's native x64 helper and 64-bit COM view. `internal32` is the default; `external32` selects the x86 helper. Temporary engine enable/mode/extension choices are restored after the owned player closes. The runner verifies the actual helper architecture/hash and the expected DLL inside the actual engine process; an internal fallback fails setup. CPU covers both player and descendant helper. `--profile-session <unused-number>` attaches the installed sampler to the actual graph process after setup, including the x64 helper; it stops the sampler before closing the player.

Counter snapshots are refreshed after setup while still paused at 22:20, immediately before the measured Run. Module snapshots use explicit WOW64-aware native enumeration. Comparisons reject changed engine, filter bitness, active dependency hashes, polling cadence and window geometry.

MPC-VR may reset only `FramesDrawn` in the first Run interval. The raw reset flag remains saved. Summary validation permits that specific first-two-seconds case only when the dropped counter started from zero and stays independently monotonic, with continuous renderer and media position. Such drawn resets are explicitly listed in the summary; drawn frames never serve as a drop-percentage denominator. Decreases in dropped frames, later/unclassified resets, renderer replacement, seek discontinuities, missing values and inconsistent deltas fail validation. `test_compare_playback.py` checks these gates, including retention of first-interval drops and combined player/helper CPU and private-byte accounting.

## Compact-grouping diagnostics

Serial offline checks were reauthorized on 2026-10-26, on one logical CPU at
Above Normal priority. Playback remains paused. The [grouping checkpoint](../../docs/performance-20261026-compact-grouping.md)
records the earlier validation and the subsequent group-key-view work.

The following are additional harness entry points:

```powershell
render_subtitles.exe --grouping-contract
render_subtitles.exe --allocation-probe-contract
render_subtitles.exe --group-view-contract
render_subtitles.exe --diagnostics --bitmap-cache-items 256 default.ass <fonts> <new-phases.csv>
python test/benchmark/summarize_diagnostics.py <control-phases.csv> <candidate-phases.csv> --output <new-summary.json>
```

`--grouping-contract` exercises overlaps, more than 128 input items, inverse clips,
reservation, optional diagnostics and retained frames. `--allocation-probe-contract`
checks xy_malloc-only calls/bytes/peak/release and nested scope restoration.
`--group-view-contract` checks miss ownership,
hit identity, copying/assignment, mutation, eviction, bypass and retained pixels.
`fixtures/grouping-stress.ass` exercises compact membership storage and a rectangle
that spans more than eight output groups.

Diagnostic CSV columns overlap: render-core/composite totals contain nested
phases. Final draw growth counts measure capacity changes during append;
pre-reservation allocations are excluded. The scoped allocator probe sees
xy_malloc traffic only, not ATL/STL/shared_ptr heap allocations. Native diagnostics
are off by default, change no COM API and are not enabled in ordinary playback.
The fresh collector also records the active MPCSubtitleHelper.ax path/hash.

If x64 native linking reports LNK4020, the observed successful rebuild was a
serial subtitles-library Rebuild, harness Rebuild, then serial DLL links, all
with v145/Release and `/nr:false /m:1`. Keep generated PDBs with their actual DLLs.

## Standalone console performance app

`render_subtitles.exe --benchmark` verifies Above Normal process priority and
restricts the process to one allowed logical CPU. Rendering runs serially on the
calling thread. `--cpu N` selects a CPU in the current processor group; the default
is the highest allowed CPU represented by the process affinity mask. The actual
mask and priority are recorded in the log. All correctness entry points also run
on one logical CPU at Above Normal priority.

```
render_subtitles.exe --benchmark --mode render --cpu 7 --bitmap-cache-items 256 default.ass fonts render.csv
render_subtitles.exe --benchmark --mode parse --cpu 7 default.ass fonts parse.csv
render_subtitles.exe --benchmark --mode composite --cpu 7 default.ass fonts composite.csv
```

`render` times the production RenderEx path. `parse` times ParseScript plus
temporary subtitle cleanup, including list destruction; it produces no pixels.
`composite` prepares actual draw inputs outside the timed section and times
production rectangle splitting/grouping, bitmap lookup and drawing. Input
preparation, output hashing and CSV writes are excluded from this selected
interval. Replay modes use the sample's 1080p geometry and 24000/1001 timing.
Geometry is initialized once; sequential timestamps preserve cache history.
XY allocation counters are collected only in complete-render mode. Nested phase
timings overlap. Thread CPU time has coarse per-frame resolution; use aggregate
CPU totals alongside wall-time distributions.

`--first-frame` and `--last-frame` select inclusive source frame indices. Defaults
are 32128..34555 (22:20Ã¢â‚¬â€œEOF). Every new process starts a fresh renderer and cache
state; subsequent frames warm caches naturally. `--no-hash` and
`--no-diagnostics` are optional timing-only controls, separate from correctness.

`run_native_benchmark.py` runs control/candidate serially in alternating order,
records executable/script/font hashes, checks all frame pixel hashes (except in
parse mode), and reports total/median/p95/p99/max time. For example:

```
python test/benchmark/run_native_benchmark.py --control control/render_subtitles.exe --candidate candidate/render_subtitles.exe --script default.ass --fonts fonts --output new-results --cpu 7 --rounds 2
```

The build-only `/p:XyBenchmarkOwnedGroupKeys=true` control materializes temporary
owned group-key snapshots. Normal x86 builds use borrowed synchronous probes;
x64 retains owned snapshots because complete-render timing was inconclusive.
Use `/p:XyBenchmarkOwnedGroupKeys=false` to test borrowed x64 probes. Rebuild
the subtitles library, relink the console app and preserve its EXE/PDB before
switching the property. Keep compiler, dependencies and other sources identical.

Priority and affinity reduce scheduling variability but do not eliminate shared
cache/memory/frequency effects from other applications. These results measure
subtitle CPU work; they do not establish renderer drops during playback.

## Optional libass correctness checks

See [libass integration](../../docs/libass-integration.md) for static source builds.
`--libass-contract` tests the backend; `--filter-contract xy <XySubFilter.dll>` and
`--filter-contract vs <VSFilter.dll>` instantiate the real filter and Main page in
a hidden host, check option round-tripping and layout, and do not write registry
settings. No player or filter registration is needed. Libass performance is not
an acceptance criterion. Its visual output has a separate baseline from Native.

Focused pixel checks use `compare_rendered.py --first-frame N --last-frame M`;
missing pixel hashes are rejected. Run `python test/benchmark/test_compare_rendered.py`
for coverage-gate checks. With VS 18.11, clean-rebuild filter/console projects
after switching the group-key build property to avoid stale incremental linker
debug data; release subtitles objects embed their types with `/Z7`.

## Final draw-slot storage regression

`--inline-list-contract` also covers empty, inline and spilled final draw lists,
POSITION stability across 257 appends, layer ordering and owner release.
`fixtures/draw-slot-spill.ass` exercises 257 alternating style runs, multiple
lines, inverse clipping and an overlapping translucent polygon. Render it with
the frozen control and candidate on each architecture and compare all hashes.
A diagnostic run must report more than 1,000 input items in at least one frame
to establish that the large single-event spill path was reached. See the
[inline-storage report](../../docs/performance-20261026-inline-composite.md) for
the x64 comparisons and validation records.

## ASS command and first-parameter storage checks

`render_subtitles.exe --ass-command-contract` compares command recognition with
the legacy longest-prefix matcher in 31,200 cases and checks inline/spilled
parameter ownership and order. `fixtures/ass-command-prefixes.ass` is UTF-8 with
a BOM and covers ambiguous/unknown prefixes, case, Unicode, whitespace, empty
arguments and nested transforms. Compare it with the preserved control on each
architecture.

For event-spike measurements, use complete sequential sample passes and select
frames from source event start times before examining candidate timings. Keep
parse-only results separate: their cache history differs from full rendering,
and they do not produce pixels. Always check the recorded `mode` in each
benchmark summary rather than inferring it from a folder name. See the
[event-spike report](../../docs/performance-20261026-event-spikes.md).

## Font/style and layout checks

`--font-layout-contract` checks Unicode entry borrowing and ANSI conversion
lifetimes, cached/uncached equivalence, forced defaults, all PAR compensation
modes at two factors, source edits and preservation of selected styles.
`font-style-layout.ass` and `font-style-layout-ansi.ass` compare with a frozen
legacy renderer across single-word alignment/wrapping, whitespace/breaks, NBSP,
large/tiny words, multiple lines, font resets and polygons. The Unicode fixture
has a BOM; the ANSI fixture intentionally does not.

Source-line profile attribution must distinguish CString copy-on-write/refcount
work from actual character-buffer allocation. `CloneData` can share an existing
buffer, and `Trim()` does not clone a font name when no trimming is needed.
See the [font/layout report](../../docs/performance-20261027-font-layout.md).

## Frame-owned grouping scratch

The [scratch-storage report](../../docs/performance-20261027-grouping-scratch.md)
compares contiguous metadata plus frame-owned memberships with the frozen
font/layout build. Keep per-event shadow/outline/body order and the existing
rectangle algorithm. The grouping contract, large draw-slot-spill and inverse-clip
fixtures, retained-frame checks, full sample at 256/zero cache entries and
composition replay are the relevant pixel gates on both architectures.
Full-render and composition-only timings have different scope; record them
separately and keep builds outside timed runs.

## General event preparation and layout reuse

`--prepared-layout-contract` checks the prepared path against a renderer with
`SetPreparedLayoutCacheLimit(0)`, including context edits, retained frames and
byte admission. `event-layout-reuse.ass` supplies varied legacy comparisons;
`event-layout-load.ass` supplies independent two-font animated text load.
The console prints prepared-layout hits/builds/fallbacks/updates and accounted
bytes. Every first-use preparation cost stays inside render/parse timing.
See the [implementation and results](../../docs/performance-20261027-event-layout.md).


## General spike-reduction checks

The console now has 28 focused contracts. New entry points are:

- `--maintenance-contract`: verified order reuse, ties/order edits, same-segment
  streaming insertion, deadlines, seeks, invalidation and FRAME/FPS fallback.
- `--lazy-intern-contract`: one probe, owned copies only on misses, identity and
  zero/runtime capacity. Existing group-view/key contracts check retained keys.
- `--clipper-retention-contract`: weak mask reuse and release after MRU eviction.
- `--fused-alpha-contract`: every opacity, coverage pairs, masked/unmasked
  body/border subtraction, clipping, subpixel offsets, vector/tail widths,
  arbitrary destination bytes and planar/multicolor fallbacks against legacy.
- `--lookahead-contract`: blank-request preparation, text/polygons, edits before
  activation, static/metric fallback, insertion, seeks/expiry and retained frames.
- `--paint-bundle-contract`: all requested-layer combinations, one shared owner,
  post-factory painting, alias lifetime and cycle-free release.

`fixtures/spike-bursts.ass` has twelve independent 64-event bursts with two
fonts, text, drawings, styled runs and changing raster/layout properties.
Compare all 2,428 timestamps with a frozen legacy renderer on both architectures.
For its shorter timing load, use `--first-frame 32128 --last-frame 32450`.
Production code has no knowledge of this fixture or the private media.

Full rendering includes the synchronous lookahead work inside `RenderEx`,
including blank-frame requests. The nested `parse_ms` field excludes that work;
parse-only and composition replay have different scope and cache histories.
Do not present the parse-phase reduction as the complete CPU saving. The 4 ms
allowance is checked between source events, so a single construction can
overshoot it. Request cadence affects available preparation opportunities.
The console logs attempted/retained/skipped events, preparation time and the
largest unit, together with order reuse and expiry-scan counts.

Current default admissions are 64 MiB syntax, 64 MiB/16,384 reconstructed states
and 32 MiB prepared layouts. Cache limits are accounted ownership bounds;
measure process peak commit separately. Raster/bitmap cache capacities are not
blanket-enlarged. See the [report](../../docs/performance-20261027-spike-paths.md)
and [portable measurements](../../docs/benchmarks/20261027-spike-paths.json).


## Packet arrival and recurring playback spikes

`streaming_subtitles.vcxproj` builds a separate console with the same shared
native library and the existing harness's font, scheduling and pixel helpers.
It removes native future entries and feeds decoded events through `Add` only
when their presentation time is within the configured delivery lead. Dataset
reading/sorting and header/style setup are excluded; relevant insertion and
RenderEx work, including initial active events, are timed. No video is played.

Build this project directly with the same SolutionDir/Release/platform/toolset
arguments as `render_subtitles.vcxproj`. For example, from a Developer shell:

```powershell
MSBuild.exe test\benchmark\streaming_subtitles.vcxproj /p:SolutionDir=F:\Git\xySubFilter\ /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /m:1 /nr:false
python test/benchmark/run_packet_benchmark.py --exe bin/lib_18.0/x64/Release/streaming_subtitles.exe --script benchmark-results/media/default.ass --fonts benchmark-results/media --gold benchmark-results/source-control/render.csv --output benchmark-results/packet-leads-new --rounds 4
```

The runner compares lookahead off/on within this optimized build, using a
256-entry bitmap cache, CPU 7 and Above Normal priority on the current host.
Defaults are 0/250/1000 ms packet lead, frames 32700..32830 and the independently
preselected 32780 burst. Change `--first-frame`, `--last-frame` and
`--burst-frame` together for another region. The source frame clock is currently
24000/1001 fps at 1920x1080, matching the primary harness. Use `--allowance-us`
for a different nonzero lookahead allowance, at most 4000 us.

Every output frame must match the supplied full-script gold. Zero lead must
prepare zero future events. Complete-window timing is retained; separate
`steady_metrics` excludes only the first independently initialized model
request. Later activation, new-font and raster spikes remain in the steady
statistics. The user's acceptance permits one isolated pause-resume drop;
this console does not perform an actual pause/resume test.

This is a model of packet availability, not a measurement of actual DirectShow
packet lead, pin decoding/locking, graph concurrency or dropped frames. Actual
arrival/request cadence determines how much lookahead can help embedded tracks.
See the [results and limits](../../docs/performance-20261027-spike-paths.md).

### Retained preparation/draw checks (2026-10-27)

The `--constant-drawing-contract`, `--static-drawing-contract`,
`--polygon-scanner-contract` and `--draw-reuse-contract` checks cover the new
native paths. Run these together with the existing 28 contracts on both x64 and
Win32. The scanner test contains a test-only legacy oracle and 2,000 deterministic
differentials; the draw test covers all five native bitmap formats, all subpixel
levels, retained frames, eviction, cache budgets and the 128-item blend threshold.
It explicitly flushes the bitmap MRU on format changes because its existing keys
omit the pixel format. This is a harness requirement, not a new format-cache fix.

[prepared-draw-reuse.ass](fixtures/prepared-draw-reuse.ass) is an independent
160-event regression input with varied text, paths, step/nested drawing commands,
clips, motion, low opacity and karaoke. Compare it with the frozen preceding build
as well as comparing the complete private sample and existing public fixtures.

Diagnostic CSVs now include descriptor hits/misses, ordinary machine/draw/overlay
key factory allocation counts, and charged descriptor bytes. These are counters
for the tracked cached-event draw path, not every C++/CRT/ATL allocation; they
supplement `xy_alloc_calls`. Retained descriptor metadata has a separate 32 MiB
budget (4 KiB charged per plan); raster owners are weak between requests and remain controlled by
existing image MRUs. Frame-scoped pins retain strong owners while composition
uses them, then release them; copied frame slots preserve the pin lifetime. Frame-specific additive decisions do not mutate descriptors.

The [implementation and comparison report](../../docs/performance-20261027-top-three.md)
and [analysis script](../../docs/benchmarks/20261027-top-three-analysis.py) document
four fresh-process passes per build and architecture, the seven review spikes,
all 17 preselected activation bursts, and following requests. These are native
console measurements, not actual renderer drop counts. No playback is needed to
reproduce this comparison.

### Zero-lead parsing checks

`--scalar-parameter-contract` adds collision/eviction, ownership, empty/reset and
legacy syntax, style, CRT locale/rounding, exact-real bounds, signed zero and
Win32 24/53-bit precision checks for the renderer's bounded
scalar parameter cache. Run it with the existing 32 contracts on x64 and Win32.
[scalar-parameters.ass](fixtures/scalar-parameters.ass) supplies an independent
pixel comparison against the preceding build.

The packet probe accepts optional trailing flags `--diagnostics`, `--no-layout`,
`--no-draw-reuse` and `--no-scalar-cache`. Diagnostics reset phase counters per
request and report layout/draw construction plus scalar hits/misses. Feature-off
runs isolate mechanisms; final timings use the default uninstrumented scope.
Always verify the CPU affinity and Above Normal line in every child log. Python
launchers must pass a pointer-sized Windows process handle and check affinity
setter results; an unchecked ctypes call can fail and leave the default CPU.

See [the zero-lead report](../../docs/performance-20261027-zero-lead.md) for the
frozen latest/older controls, selected-core and original-core comparisons,
neighboring requests, rejected prototypes and exact-output gates. These are
packet-delivery models, not observed playback drops. No playback is run.

### Cold tag/parameter storage checks (2026-10-28)

`--tag-storage-contract` supplements the existing 33 contracts with 6,000
parameter operations, alias-safe growth, reserve/truncate/refill, nested ownership,
5,000 stable tag addresses and separate numeric spills. It prints header sizes
and spill-growth counts for common arities; those are storage/subset-allocation
measurements, not a count of every CRT allocation or a rendering speedup.

Compare [tag-storage.ass](fixtures/tag-storage.ass), an independent 100-event
short/long/malformed-parameter fixture, against the frozen preceding executable.
Use the full sample, all existing scripts and both bitmap capacities as before.
The packed storage changes capacity accounting, so report admission statistics
and peak process commit alongside timing rather than attributing every gain to
allocation speed alone. Keep full render and isolated parse/reconstruction scopes
separate. Continue one rendering thread/core at Above Normal; no playback.

See [the cold-storage implementation/results](../../docs/performance-20261028-tag-storage.md) for repeated timings, memory, rejected alternatives and exact-output gates.

## Render layout and allocation regression tests

`render_subtitles.exe --layout-contract` exercises wrapper output/layout separation,
static invalidation, cropped combining and both alpha conventions, retained frame
ownership, allocation faults, native/libass geometry and recovery, and reentrant
consumer queue invalidation. It includes 1080p and 8K output/layout combinations.

`render_subtitles.exe --layout-filter-contract xy <XySubFilter.dll>` and
`render_subtitles.exe --layout-filter-contract vs <VSFilter.dll>` load the actual
More pages and test staged typing, Apply/Cancel, bad persisted dimensions and
atomic settings validation. HKCU is redirected for the test process to a unique
subtree under `Software\xySubFilterLayoutTests`, removed on normal exit. Run each
command in a subprocess with a 30-second timeout; after a killed process, remove
only its dedicated test subtree. These tests do not need filter registration or
playback. Run matching executable/DLL architectures on both Win32 and x64.

The supported bounds and validation evidence are documented in
[the layout implementation report](../../docs/render-layout-fixes-20261030.md).

Use `run_layout_regressions.py --exe <render_subtitles.exe> --xy-dll <XySubFilter.dll>
--vs-dll <VSFilter.dll> --output <results.json>` to run all three new contracts with
a 30-second watchdog per process and cleanup of that process's isolated test
settings after a failure or timeout.

## Consumer queue regression tests

Run matching Win32 and x64 executable/DLL pairs:

```powershell
python test/benchmark/run_queue_regressions.py --exe <render_subtitles.exe> --xy-dll <XySubFilter.dll> --output <results.json>
```

The runner uses a 45-second child watchdog and the same isolated registry cleanup
as the layout tests. It records the tested DLL hash. `--queue-state-contract`
checks partial-boundary coalescing, in-flight delivery races, failed deliveries,
connection replacement epochs, future packets and delay/speed overflow handling.
`--queue-filter-contract native|libass <dll>` connects the actual DLL to a
128-slot consumer and feeds ASS through a real DirectShow subtitle pin. It checks
constant, jittering, VFR and zero frame durations; changing geometry; future and
late packets; synchronous refill from Clear; video-time delivery and partial
invalidation with delay/speed; global settings; seeks; failure propagation; and
consumers without the optional Clear interface. No DLL registration is needed.

`--alpha-wrap-contract` preserves the existing out-of-range ASS alpha behavior:
both native and libass interpolate toward `F00` before narrowing to eight bits.
Forty frame comparisons cover both `&F00&` and `&HF00&` spellings against static
in-range references. This test does not reinterpret the authored effect.

For real madVR validation, `benchmark_filter.py` accepts `--renderer madvr`,
`--media <mkv>`, `--seek-seconds <seconds>` and `--paused-seconds <0..300>`.
It restores renderer/engine settings and temporarily disables visualization
routing when an external helper is requested. A requested renderer or helper
that is not actually loaded fails validation. The Debug/LOCAL build and loaded
DLL hashes are still verified. The paused hold allows inspection of madVR's
subtitle queue overlay; TCP 1150 alone does **not** report subtitle queue depth.
