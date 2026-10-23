# ASS parsing and allocation optimization

This continues the [style-hash experiment](performance-20261023.md). The workload is the default **Signs & Songs [eng] (ass) [default]** track, Zoom index 0, in `Subtitles_CPU_killer_on_end_credits.mkv`. Playback uses Debug (`LOCAL`) **Z:\zpAI\zplayer.exe**, LAV and madVR, from **22:20 through EOF (24:01.272)**. The filter is optimized Win32 Release with symbols, built with the installed v145 toolchain. The machine and original sample hashes are recorded in the earlier report and capture metadata.

## Retained changes

1. **Retain immutable syntax for active animated events.** Parsed ASS tag blocks stay with their event under a 16 MiB conservative admission budget. Ended events are released; backward seeks, stream changes, initialization and text edits invalidate syntax. Rejected entries use the existing shared MRU. Animation evaluation still runs at each timestamp.
2. **Convert numeric arguments once.** Parsed tags retain the same CRT `wcstod`/`wcstol` results and preserve bases, transform arities and malformed-value behavior. Omitted arguments are explicitly zeroed. The first number is inline; extra arguments allocate only when needed. An initially tested seven-number inline payload increased cache pressure and was replaced by this compact representation.
3. **Remove temporary allocation work.** Lines paint into final draw-item slots while preserving line, word and inverse-clip region order. String interning borrows inputs for lookup and retains owned copies on misses. Drawing-coordinate parsing advances a pointer rather than repeatedly deleting consumed prefixes.
4. **Consolidate shared allocations and reuse layer work.** Draw items, keys and paint machines use combined object/control-block allocations where applicable. Only requested layers receive keys. Dirty-rectangle queries reuse painted overlays; unclipped subtitles share a stateless clipper machine.
5. **Reuse raw polygon geometry.** `CPolygon::ParseStr` can copy vertices from the existing bounded path cache before tokenizing the drawing again. Reuse requires exact nonzero script scales, avoiding tolerant matches and text-path collisions. Baseline and current font-scale metrics are recalculated. Cached vertices remain immutable and untransformed; painting transforms a separate copy. This adds no cache.

The changes are in the shared renderer used by both filter paths. They add no COM API fields or player options. The knowledge graph is [updated](knowledge-graph.md).

## End-to-end playback

These are **unprofiled** captures, each approximately 102 wall seconds. The collector verifies EOF, the default subtitle, continuous available renderer counters, and matching player/media/other-filter identities.

| Build / capture | Renderer drops | Player CPU seconds | Peak private MiB |
| --- | ---: | ---: | ---: |
| Hash-only control (repeat) | 1,017 | 78.03 | 581.5 |
| Parser/allocation pass | 800 | 78.19 | 595.0 |
| Parser/allocation repeat | 801 | 77.36 | 589.2 |
| Geometry reuse (retained) | 612 | 74.81 | 596.5 |

The retained build reduces renderer drops from **1,017 to 612 (39.8%)** and total player CPU from **78.03 to 74.81 seconds (4.1%)** against the repeated hash-only control. The parser/allocation stage was separately repeated at 800 and 801 drops before adding geometry reuse. The final geometry build has one unprofiled measurement; its CPU profile is diagnostic evidence separately identified below.

CPU and private bytes cover the player, decoder and renderer together. Peak private bytes increased by about **15.1 MiB** in this comparison. The new syntax cache has a 16 MiB accounted limit, and its retained event entries return to zero at EOF; admission accounting and Windows private bytes measure different things. Shared syntax is conservatively charged to every retaining event.

`FramesDrawn` and renderer average FPS describe renderer presentation/refresh behavior. They are not source-frame counts and are not used as a denominator for a dropped-source-frame percentage.

[Playback comparison JSON](../benchmark-results/ass-final-comparison.json) contains the validated capture summaries. Raw capture directories are `hash-repeat-unprofiled`, `parser-final-unprofiled`, `parser-final-unprofiled-02` and `geometry-reuse-unprofiled`.

## Fixed-work renderer measurements

The native check renders every one of the **2,428 source-frame midpoints** at 1920x1080, independently of playback drops.

| Renderer | Mean ms | p95 ms | p99 ms |
| --- | ---: | ---: | ---: |
| Unmodified source control | 70.17 | 180.02 | 253.75 |
| Previous hash-only pass | 62.16 | 157.30 | 237.75 |
| Parser/allocation pass | 35.25 | 83.29 | 166.33 |
| Geometry reuse | 31.55 | 74.17 | 147.17 |

Geometry reuse improves mean time by another **10.5%** over the parser/allocation pass. The full change is **49.2% lower** than the hash-only pass, or **55.0% lower** than unmodified source. These are offline wall times, secondary to the playback measurements. There remain **1,030** offline frames over the **41.708 ms** source-frame budget.

Actual tag-list parses fall from **3,330,255 to 177,985 (94.7% fewer)** in the fixed workload. The final event cache reports 2,495,252 hits and 1,276,926 fallbacks; 1,098,941 fallbacks hit the legacy MRU. Peak accounted event retention is 16,777,198 bytes, with zero entries/bytes at EOF.

[Final pixel/timing comparison](../benchmark-results/geometry-reuse/pixel-comparison.json).

## Pixel and lifetime validation

- **All 2,428 ending frames match the original control**, including 2,184 nonempty frames. SHA256 covers bitmap positions, dimensions and visible RGBA bytes, excluding cache identities and pitch padding. Fonts are loaded privately from the sample's attachments.
- **54 fixture images match** the preserved pre-numeric-cache renderer: numeric prefixes, transform arities, colors/alpha, vector/inverse clips, karaoke, drawings and malformed tags.
- A separate **2,428-frame omitted-argument** case matches unmodified source, with visible subtitles throughout.
- A geometry fixture matches unmodified source across 36 visible frames followed by expiry: animated scaling/rotation, multiple drawing scales, baselines and fractional coordinates.
- The earlier frame-assembly changes also matched a 2,428-frame inverse rectangular clip / multiline / fractional drawing case.
- Cache contracts compare enabled/bypassed output across animation, expiry, backward seeks, text edits, malformed tags, tiny budgets and stream invalidation.
- String-intern contracts check IDs, owned storage after caller mutation and live-value survival after eviction.
- Geometry contracts exercise a real cache hit, compare cold/warm vertices and changed layout metrics, verify exact scale matching and owned buffers after eviction, and reject a simulated text-path collision at zero script scales.

`VSFilter`, `XySubFilter` and `xy_sub_filter_consumer` build successfully with the retained shared changes (Win32 Release, v145). Existing compiler warnings remain. Python compilation and whitespace checks pass. The playback test exercises XySubFilter with madVR; the native check exercises shared RGBA production. Other architectures and every other subtitle format have not been tested.

## Profiles and further work

The event-syntax profile showed decimal conversion at about 9.1% inclusive samples, motivating numeric caching. In the later parser/allocation profile it was about 3.1%; polygon construction was about 8.0%, motivating geometry reuse. Draw-item/key allocation and repeated overlay work motivated the intermediate allocation passes.

The final profile covers elapsed 20-100 seconds (roughly 22:40-24:00): **7,746 samples, 7,737 with stacks, zero lost events**. All sampled XySubFilter instructions came from the intended workspace DLL; the installed autoload helper was also loaded but had no sampled XySubFilter instructions in this window.

| Inclusive sampled share | Before geometry reuse | After |
| --- | ---: | ---: |
| `CPolygon::CPolygon` | 8.05% | 1.43% |
| `CPolygon::ParseStr` | 4.27% | 0.52% |
| Decimal conversion | 3.07% | 0.27% |
| `CRenderedTextSubtitle::GetSubtitle` | 25.34% | 20.08% |

The retained build's diagnostic profiled playback recorded **582 drops**, versus 853 in the preceding profiled parser/allocation pass. These profiled captures are separate from the unprofiled claims above.

Remaining sampled costs include **GetSubtitle (20.08%)**, **heap allocation (14.25%)**, **DrawItem::AlphaBltDraw (9.06%)**, and **tag evaluation/parsing (5.99%)**. Inclusive shares overlap, change with workload, and must not be added. Optimized x86 inlining can omit callers; these are sampled observations rather than exhaustive phase timings.

The next priorities are to retain an event's text spans/command sequence so frame evaluation avoids repeated substring/list reconstruction, and to amortize per-frame draw-item/key storage while respecting shared ownership. These require the same cold/warm, seek/edit/lifetime and pixel gates, followed by unprofiled playback. Polygon numeric parsing is now small enough that another rewrite there has lower priority.

[Final CPU functions](../benchmark-results/geometry-reuse-profiled/cpu-functions.csv), [module paths](../benchmark-results/geometry-reuse-profiled/cpu-module-paths.csv), [trace quality](../benchmark-results/geometry-reuse-profiled/cpu-analysis.json), [profiled comparison](../benchmark-results/ass-profiled-comparison.json).

**Zero-drop playback has not been reached.** The remaining long frame times require further reduction even though the average is now below one frame budget.

## Reproduction and saved state

See the [benchmark README](../test/benchmark/README.md). `benchmark_filter.py` temporarily selects a workspace DLL through 32-bit per-user COM registration, runs the capture, closes its player, and restores registration and the completion action. The installed Program Files DLL is unchanged.

The retained build is `benchmark-results/geometry-reuse`, DLL SHA256 **7960d69226fe31f996b16f2119e7619ba3d6e52d8ca0f3cdf2d2387df82e3770**. Its [manifest](../benchmark-results/geometry-reuse/build-manifest.json) preserves binary/source hashes and matching DLL/PDB/executable snapshots. Earlier experiments are retained in `event-tags`, `numeric-tags`, `compact-numeric`, `allocation-pass`, `shared-allocation`, `layer-reuse` and `parser-final`. The initial `hash-memory-control` capture had a cold first-second CPU spike; final claims use the repeated control above.

After testing, the temporary per-user registration is absent, the installed DLL retains its original SHA256, completion action 2 is restored, both final benchmark players are closed, and sampler session 79 stopped successfully. No software was installed by the agent; Yasm was supplied by the user.

The subsequent [reconstruction and allocation report](performance-20261023-reconstruction.md) records the continuation of this work and later playback results.
