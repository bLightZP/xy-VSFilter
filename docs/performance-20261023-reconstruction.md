# Subtitle reconstruction and allocation follow-up

This continues [the ASS parsing work](performance-20261023-ass.md). The fresh control is the retained `geometry-reuse` build. Measurements use Debug (`LOCAL`) `Z:\zpAI\zplayer.exe`, the sample's default embedded ASS track, madVR, and 22:20 through EOF (24:01.272). The subtitle DLL is Win32 Release with symbols, built using installed VS 2026/v145 and user-supplied Yasm. No software was installed.

## Stopped for PC use

Further optimization and testing were stopped at the user's request. The latest stable source is compiled into all three Win32 Release DLLs with matching frozen XySubFilter symbols. The latest ordinary 256-entry run completed with **zero new drops from 22:20 through EOF**; the preceding ordinary 256-entry repeat was also entirely drop-free. Other final runs had one first-interval drop and none later. The final candidate matched all 2,428 source-frame subtitle bitmaps at both cache capacities. See [remaining work and final saved results](optimization-review-later.md) and the [build manifest](../benchmark-results/word-keys-exact/build-manifest.json). No further tests are running.

The version-only **v3.2.1.900** release preserves the measured renderer source. A compact [measurement summary](benchmarks/v3.2.1.900.json) is committed for review without the ignored local capture directories.

## Changes tested

1. **Source cursor and compaction:** consume source offsets instead of cloning each remaining suffix. Compact lines in place, sharing single words and polygons, and copy text only when concatenation needs it.
2. **Inline sequences:** store the first word/line/key directly, with vector spill storage for longer sequences. Word factories use combined shared allocations. Preserve wrapping, order, ownership and line iteration.
3. **Composition scratch:** one explicitly owned, aligned alpha-mask buffer per synchronous frame composition replaces allocation and shared-pointer bookkeeping for each draw. No scratch pointer escapes into a cache or delivered bitmap.
4. **Retained text spans:** the existing bounded active-event ASS cache retains tag/text boundaries and raw text. Evaluation still applies animation at each timestamp. Unicode entries avoid redundant conversion/style lookup. Cache admission remains 16 MiB with text storage included in accounting.
5. **Polygon ownership:** retain the immutable raw path owner on a cache hit; make a separate mutable copy when transforming. Recompute current layout metrics. Single-color zero-alpha draws skip mask generation/blending after computing identical bounds; focused tests check preservation of existing background pixels.
6. **Event storage:** store the first effect and first line inside the event. Use a per-renderer pool whose fixed capacity is calculated from a 1 MiB storage budget (3,590 objects before the interval metadata was added). Recycling destroys old content and constructs a fresh default object in the same storage. Spill vectors, words, clippers and effects are released before storage is retained. Clear the pool on invalidation and deinitialization; compile-time storage accounting stays below 1 MiB.

7. **Shared layer geometry:** when fractional positions match, share geometry keys and painted overlays across shadow/outline/body layers. Opaque-box geometry remains separate from the body. Draw items preserve their independent full position, color, clipper and flags. Native overlay queries fell from 7.86 to 4.81 million. Combine the paint-machine object and shared control block in one allocation.
8. **Stable reconstruction intervals:** track exact integer-millisecond constant intervals before/after evaluated transforms. Reject continuous interpolation, vector clipping, scrolling and the origin workaround. The first candidate retained only small single-polygon states, with 4,096 entries and a 16 MiB accounted budget. The retained candidate also permits small single-word text states, up to 8,192 entries under a shared 32 MiB accounted budget (4 KiB per polygon state, 8 KiB per text state); this accounting is distinct from process private bytes. Validate interval, event duration and source identity on reuse; clear on expiry, backward seeks, edits and invalidation. The polygon-only native run avoided 2,833,343 reconstructions. The larger text/polygon cache avoided 3,083,917, peaked at 5,918 entries / 26,157,056 accounted bytes, and released all retained states by EOF. The user explicitly authorized a reasonable increase in memory usage.

9. **Per-word keys:** retain at most three geometry and three draw keys per word. Keys retain no parent word or rendered image. Geometry matches exact identity/style/scales/positions; draw matches require equal hashes as well as the original equality fields. Tests include a near-equal blur value with a different hash, text mutation, eviction and lifetime checks. This avoids repeated key allocations for retained states.
10. **Bitmap capacity experiment:** the original 64-entry cache is smaller than the sample's average ~94 output groups per frame. Testing 256 entries increased native bitmap/group-key cache hits from 4,392/228,488 (1.9%) to 102,469/228,488 (44.8%), with about 10.3 MiB extra native peak commit. Output matched exactly. Native wall times did not improve, so playback CPU and repeats determine whether to select it. The runtime override is temporary and independently recorded/restored.

## Playback experiments

These are individual unprofiled runs with matching player/media/filter settings. Small differences need repeats; cumulative renderer `FramesDrawn` and average FPS reflect presentation refresh and are not source-frame denominators. Counters present before resume are subtracted; new first-second drops remain included.

| Candidate | New renderer drops | Process CPU seconds | Peak private MiB |
| --- | ---: | ---: | ---: |
| Fresh control (`geometry-reuse`) | 599 | 77.5625 | 593.7 |
| Frame composition scratch | 190 | 79.0000 | 591.4 |
| Retained text spans | 202 | 76.5625 | 595.5 |
| Combined word allocation and inline group keys | 142 | 74.1719 | 589.7 |
| Shared polygon paths and zero-alpha shortcut | 153 | 77.7812 | 586.0 |
| First effect inline | 182 | 73.6719 | 590.7 |
| Inline first line and 1,024-object pool | 157 | 75.0000 | 587.3 |
| Budget-based pool and polygon keys | 86 | 74.5469 | 596.3 |
| Shared layer geometry | 115 | 70.9844 | 583.2 |
| Stable reconstruction intervals | 1 | 61.4062 | 599.8 |

Composition scratch produced a large playback gain without decreasing process CPU: more work completed in time. Later small single-run changes fluctuated, so their individual playback benefit is uncertain. Stable reconstruction intervals produced the largest subsequent improvement. Earlier cursor/inline-list runs collected with the sampler recorded 629/605 drops and are diagnostic captures, not unprofiled comparisons.

The 16 MiB polygon-only candidate had one first-second drop in its initial unprofiled run and profile, with none later. A subsequent capture recorded 90 drops / 69.7969 CPU seconds. Its attempted extra TCP observer timed out before capture, and cannot explain the later drops; this result prevents a repeatability claim for that candidate and motivated the larger text/polygon cache.

The first complete 32 MiB text/polygon-cache run recorded one new drop in the first one-second polling interval and none later. First-second subtitles begin at 22:20.360, so the cause is not established. A separate diagnostic using 50 ms polling for the first two seconds recorded **zero new renderer drops for the entire 22:20-EOF interval**. Its different polling cadence is kept out of the ordinary comparison. The normal-cadence repeat recorded seven ending drops and 67.5313 CPU seconds, so the larger state cache alone still lacked repeatability. Per-word key retention was tested next. Its first playback pair recorded one first-second drop and zero ending drops at both bitmap capacities (62.4375 CPU seconds with 64 entries, 59.2500 with 256). The completed repeated pair recorded one first-interval drop at 64 entries and zero full-window drops at 256. A third ordinary 256-entry run was also entirely drop-free; the final sampled run recorded one first-interval drop and none later. Full results and CPU/memory measurements are saved in the review file.

An initial 32 MiB playback attempt aborted because a diagnostic polling edit accidentally reached the CPU sampler. It is preserved as `retained-states-unprofiled` with an explicit error, excluded from comparisons, and its registration/settings were restored. The corrected collector passed a two-sample self-process smoke check before the complete runs.

## Pixel and behavior gates

Preserved native executables compare every one of the 2,428 source-frame midpoints at 1920x1080 against the original source renderer (2,184 nonempty frames). Hashes include bitmap position, dimensions and all visible RGBA bytes. They exclude cache IDs and pitch padding. This checks exact subtitle bitmap output independently of playback drops; it is not a captured-video comparison.

The preserved 54-case fixture control covers numeric tags, transforms, clipping, karaoke, drawing and malformed tags. Additional scripts exercise mixed HTML/ASS, whitespace and wrapping, drawing/text transitions, fractional geometry, inverse clips and transparent layers over an existing background. Contracts check cache bounds/invalidation, seek/edit behavior, string ownership, geometry ownership after eviction, inline spill/truncation, scratch mask reuse/release, zero-alpha blending, effect isolation and event-pool reset/limits.

## Profiling and remaining work

The intermediate text-span profile samples elapsed 25-65 seconds (roughly 22:45-23:25), with zero lost events. Inclusive sampled shares included `GetSubtitle` 19.77%, allocation 11.40%, tag evaluation 7.64%, freeing 5.97%, and alpha-mask composition 1.73%. Inclusive values overlap. Optimized x86 stacks can omit inlined callers; samples are not allocation counts or exhaustive phase timings. Complete sampled chains are preserved in `cpu-stacks.csv`.

Existing bounded caches already showed approximately 99.94% overlay hits and 99.97% raw-path hits in the native workload. Increasing those cache limits has lower priority than avoiding reconstruction and per-draw bookkeeping. The larger retained-state cache reduced native mean rendering to 16.07 ms, p95 to 35.11 ms, p99 to 77.44 ms, and the number over 41.708 ms to 67, with identical output. The previous geometry-reuse control measured 31.55 ms mean / 74.17 ms p95 / 147.17 ms p99 and 1,030 over-budget frames. The polygon-only interval profile uses the same elapsed 25-65-second window: 3,858 samples, 3,848 with stacks, zero lost events. All sampled XySubFilter instructions came from the workspace candidate; the installed autoload helper had no sampled XySubFilter instructions in this window.

| Inclusive sampled share | Text-span intermediate | Polygon interval reuse |
| --- | ---: | ---: |
| `GetSubtitle` | 19.77% | 10.11% |
| Tag evaluation | 7.64% | 5.18% |
| Allocation | 11.40% | 9.82% |
| Freeing | 5.97% | 5.68% |
| Path-key hashing | 1.55% | 0.34% |

Remaining sampled work includes draw-item/key allocation, grouping and alpha blending. Shares can increase when other work shrinks; absolute sample counts and playback results are needed before calling that a regression. Final playback captures are complete; final trace analysis and additional regression scripts are deferred. Remaining first-second behavior needs separate diagnosis. No zero-drop claim is based on a partial or failed capture.

## Reproduction and saved state

See [benchmark instructions](../test/benchmark/README.md). Generated captures and frozen DLL/PDB/validation executables are under `benchmark-results/`. Each playback wrapper restores the temporary 32-bit per-user COM registration and completion action and closes its owned player. The installed Program Files DLL is unchanged. See [final saved results and deferred work](optimization-review-later.md) and the [compiled binary manifest](../benchmark-results/word-keys-exact/build-manifest.json).
