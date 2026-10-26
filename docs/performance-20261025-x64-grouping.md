# x64 validation and grouped-draw allocation work

Work resumed on 2026-10-25 at the user's request, including x64 testing. The published v3.2.1.900 tag and binaries remain the preserved control.

## Completed prior validation

- The final x86 trace (`word-keys-exact-bitmap256-profiled`, elapsed 25-65 seconds) contains 3,636 samples, 3,632 stacks, and zero lost events. All 1,686 sampled XySubFilter instructions resolve to the workspace candidate. `GroupedDrawItems::CreateHashKey` accounts for 1.98% exclusive samples; allocation/freeing remain 7.87%/4.46% inclusive. Inclusive values overlap.
- All six previously deferred x86 scripts match the saved controls for every one of their 2,428 frames: reconstruction, transparent layers, polygon font fields, geometry, inverse clips/multiline, and omitted numbers. Raw comparisons are in `benchmark-results/resume-20261025-x86-regressions`.
- All three DLL targets built as Win32 Release previously; they now also build as x64 Release with v145 (166 existing warnings, zero errors on the first full x64 build).
- The frozen x64 control passes 12 focused contracts, matches all 54 fixture cases across architectures, and matches all 2,428 original-control subtitle bitmap hashes. Its files and comparisons are under `benchmark-results/resume-20261025-base-x64`.

## Playback controls and collector corrections

The player and both helpers were rebuilt using the user's `Z:\zpAI\+Build_Debug_MAX.bat /nopause`; successful build record is `benchmark-results/zoom-debug-20261025.json`. The main player remains x86. The x64 DirectShow path runs in `ExternalEngine64/zpmediaengine64.exe` and is verified by actual process/module architecture and the Debug build hash. The benchmark uses the sample's flagged default subtitle, from 22:20 to EOF.

The user has since selected MPC Video Renderer and a 1930x1090 main client area (approximately 1920x1080 video). LAV binaries and the current player have also changed. These are fresh paired baselines and are not pooled with the earlier madVR / 970x550 results. x86 internal playback and x64 helper playback are compared against their own matching controls.

The collector now has explicit internal32/external32/external64 selection, the matching COM registry view, strict loaded-DLL checks, and restoration of temporary engine options. Native EnumProcessModulesEx with LIST_MODULES_ALL replaces Windows PowerShell's incomplete x86 module enumeration. Its first x86 setup attempt failed this strict check before measurement and is excluded. An initial x64 run completed with a counter reset across the first polling interval; it is also excluded from continuous-counter comparisons.

Initial counters used to be read immediately after seeking, followed by metadata gathering before playback. An asynchronous helper can update its seek/renderer snapshot during that gap. Counters are now refreshed while still paused immediately before measurement and Run. First-second drops after Run remain included. The earlier reset is not silently converted to a zero-drop interval.

## Candidate changes

- Replace each group's linked list of draw-item shared pointers with the existing inline-first sequence, preserving shadow/outline/body ordering and all clipping/group boundaries.
- Construct bitmap group-key probes on the frame's stack. The shared interning cache retains an owned copy only on a miss; hits avoid creating/deleting a separate key and control block.
- Combine the owned object and shared control block when interning borrowed values misses. No new retained cache or capacity is added.
- Add a focused group-key contract covering inline/spilled storage, borrowed-versus-owned identity, drawing order, clipping, mutation, cache eviction and parent/child lifetimes.

The candidate builds all three DLLs and the harness in Win32 and x64. Both pass 13 focused contracts, 54 fixture comparisons, the 2,428-frame sample comparison, and all six extended 2,428-frame scripts. x64 output also matches at 1,024 bitmap entries. Native runs overlapped during regression coverage, so their wall times do not establish a speed-up.

The native bitmap-cache hit ratio increases from 102,469/228,488 (44.8%) at 256 entries to 111,897/228,488 (49.0%) at 1,024 entries; x64 peak pagefile commit rises from about 351.1 to 357.1 MiB. The temporary larger-capacity experiment leaves source defaults unchanged. Group-key probe query counts rise on misses because borrowed lookup precedes owned insertion; bitmap hit counts are the comparable per-group measure.

## Measurement status

Fresh controls: x64 helper recorded 53 drops / 46.640625 CPU seconds; x86 internal recorded 37 drops / 45.671875 CPU seconds. The x86 drawn counter reset in the first interval while dropped frames stayed at zero. Summary validation now permits only this classified initial drawn reset with a continuous zero-based dropped counter and lists it explicitly; dropped resets, renderer/position discontinuities and later/unclassified resets still fail. Eight metric-gate tests pass. Memory summaries include combined player/helper private bytes as well as the largest single process.

Two candidate attempts failed before measurement (TCP reconnect readiness and an asynchronous Pause race). They are excluded and restored their settings/registration. A single runner connection and repeated readiness-based Pause now handle setup. Candidate playback comparisons are underway; no speed-up is claimed yet.

## First candidate playback result

With 256 bitmap entries, the first complete x64 candidate run recorded **40 drops / 48.78125 CPU seconds**, against **53 / 46.640625** in the fresh control. This is fewer drops but 4.6% more CPU in one pair; it is not an established optimization win. Combined player/helper peak private bytes were 521.4 MiB for this candidate run. The x86 candidate window then closed normally about 14 seconds into playback, before the difficult ending. No application crash was logged; the partial capture is excluded. The queued 1,024-entry playback experiment did not run after that interruption.

Raw complete summaries are saved in `benchmark-results/resume-20261025-initial-comparison.json`. The next step is to repeat/isolate inline draw storage from the borrowed group-key probe, which adds an extra lookup and owned-key copy on misses, then profile the actual x64 helper if the full-HD tail remains over budget. The larger bitmap-cache playback experiment also remains outstanding.

## Manual interruptions and saved state

The user confirmed that Zoom Player was manually closed. The incomplete captures are classified as manual interruptions and remain excluded from performance comparisons. There is no player-exit investigation. Testing resumes from the preserved compiled candidates.

The current grouping code is an experiment, with functional gates passing but no accepted performance claim. Matching Win32 and x64 DLLs/PDBs/harnesses and manifests are saved in `benchmark-results/resume-20261025-grouped-Win32` and `resume-20261025-grouped-x64`. The stable x64 v3.2.1.900 control remains in `resume-20261025-base-x64`; the published tag/release is unchanged.

Cleanup after interrupted tests was verified: no owned player/helper/native-test/profiler processes; both 32/64-bit HKCU XySubFilter COM overrides absent; bitmap entries restored to 64; original external enable/mode/extension fields and completion action 2 restored. No installed filter was replaced and no software was installed.

## Paused at user request

On 2026-10-25 the user requested pausing. No further optimization or testing is authorized until resumed. Manual player closures are resolved and need no investigation. A later profile attempt could not access the missing Z: drive and failed before modifying settings, launching a player or starting session 85. Compiled candidates, matching symbols, pixel evidence and pending work are preserved. The published release is unchanged.
