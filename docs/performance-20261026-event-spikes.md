# Native event-parsing spikes, 2026-10-26

This batch uses the console renderer only. No playback was run. Measured runs
use one render thread on logical CPU 7, affinity 0x80, with verified Above Normal
process priority. Builds run serially at Normal priority on CPU 7 and do not
overlap measurements. The preserved control is the validated inline-composite
build from the preceding batch.

## Evidence and changes

The installed Visual Studio sampler profiled the unchanged x64 console, with
pixel hashing and optional phase diagnostics disabled. It collected 3,980 CPU
samples, 3,935 with stacks, with zero lost events. ParseScript appeared in 688
samples (17.29%); GetSubtitle in 605 (15.20%); GetParsedSSATags in 181 (4.55%).
These inclusive counts overlap. They identify tag compilation and reconstruction
as work to investigate; they are not separate additive phase durations.

The first candidate removed temporary command strings and repeated prefix hashes.
`LookupAssCommand` builds an immutable compact trie once from the existing command
vocabulary and reads UTF-16 token spans. It preserves the longest case-sensitive
match, including short commands and recognized prefixes of malformed commands.
Parameter strings keep their original ownership and contents. Numeric conversion
still uses the same CRT functions and argument-count rules.

A differential contract compares the new matcher with the legacy lookup for
31,200 tokens, covering all command prefixes, case, numbers, unknown suffixes,
ASCII pairs, truncated names and Unicode. The new `ass-command-prefixes.ass`
fixture covers actual rendering with prefix ambiguities, nested transforms,
whitespace, omitted values, malformed names, font resets and Unicode suffixes.

The trie-only candidate passed its pixel gates and reduced parsing wall time
across the 17 predefined burst frames by about 21%. Complete-render CPU did not
improve in that first ABBA batch (1.60% higher; wall time 0.57% higher). It was
preserved separately before the next change rather than treated as a proven
whole-render gain.

The combined candidate also keeps the first owned tag-parameter string inline.
Multi-argument commands spill into a vector; argument order, emptiness tests,
raw string values and numeric evaluation remain unchanged. Syntax admission
charges the actual spill capacity plus allocator overhead, with the inline
string already charged in the tag/node size. The 16 MiB syntax and 32 MiB
reconstruction budgets remain unchanged. No event work moves to loading or to
earlier frames, and animation timestamps are unchanged.

## Method

The source is the extracted default ASS track and private fonts from the test
clip, rendered at 1920x1080 and 24000/1001 fps. Source frames 32128..34555 cover
22:20 through EOF, 2,428 frames. Bitmap-cache capacity is 256 for timing runs.
Every process starts fresh; each ABBA batch is control/candidate/candidate/control.
Full rendering is the primary measurement. Isolated ParseScript mode separately
times parsing/reconstruction and cleanup with its own cache history; it produces
no pixels and is not interchangeable with the full-render phase.

Before comparison, source event start times selected 17 frames introducing at
least 1,000 events since the previous source frame. This fixed cohort measures
first-use bursts without choosing frames based on candidate timings. Per-frame
comparisons also retain source frames 33448, 33157 and 32780 from the earlier
spike analysis. Wall-time phase counters overlap; thread CPU totals are measured
separately. PC activity can still change cache, memory and frequency behavior.

Raw binaries, matching symbols, source snapshots, timing CSVs and the console-only
profile are under `benchmark-results/event-spikes-20261026/`. Final pixel/build gates are recorded below after validation.

## x64 full-render observations

The combined candidate was compared in three unprofiled batches, seven complete
passes per variant. All timing passes also passed the pixel gate.

| Measure, median complete pass | Control | Candidate | Change |
| --- | ---: | ---: | ---: |
| Parsing across the fixed 17 burst frames | 1,180.540 ms | 885.366 ms | 25.00% lower |
| Full-render parsing phase | 6.777 s | 6.225 s | 8.14% lower |
| Complete render wall time | 35.730 s | 35.728 s | effectively flat |
| Renderer-thread CPU | 34.906 s | 35.562 s | 1.88% higher |

Burst parsing decreased in every batch: 17.11%, 19.51% and 29.45%. This is retained
as a targeted latency improvement. A complete-render CPU speed-up is not
established. Individual passes vary substantially (including identical candidate
passes near 31 and 43 seconds); fixed affinity/priority do not isolate CPU
frequency, sibling-core/cache or memory traffic effects. The small pooled CPU
increase must not be presented as an overall gain.

The event syntax cache reports 488,218 hits / 200,043 misses for the control and
513,500 hits / 174,761 misses for the candidate on every x64 pass. These are
per-event retained-syntax misses, not counts of new compilations: the shared tag
MRU compiled 87,747 versus 84,036 lists. Both reach the same conservative accounted
peak of 16,777,150 bytes under the unchanged 16 MiB budget. Median whole-process
peak commit was 419.02 versus 423.59 MiB, about 4.6 MiB higher for the candidate.
This includes source storage, fonts and rendering caches; it is not the syntax
cache's physical allocation alone.

One repeat was initially named as a parse-only folder but actually ran full
rendering. Its recorded mode is `render`; it was renamed and counted only in
full-render observations. All result aggregation asserts the recorded mode.

## Remaining peaks and isolated replay

The per-frame median across seven x64 passes exceeds the 41.708 ms source-frame
budget in 57 control frames versus 48 candidate frames. These are offline wall
times, not dropped frames. The candidate still exceeds the budget, and zero
playback drops have not been established.

| Source frame | Control parse | Candidate parse | Control complete render | Candidate complete render |
| --- | ---: | ---: | ---: | ---: |
| 33448 | 99.689 ms | 72.651 ms | 158.942 ms | 139.947 ms |
| 33157 | 87.682 ms | 65.906 ms | 147.434 ms | 123.962 ms |
| 32780 | 80.503 ms | 62.188 ms | 136.893 ms | 119.114 ms |

Across the fixed cohort, complete rendering wall time fell from a median
1,913.294 ms to 1,627.815 ms (14.92% lower). This is the cohort total, not a
percentage reduction for the complete 2,428-frame pass.

The separate parse-only experiment had four passes per variant. Median thread
CPU was 7.414 s versus 7.141 s (3.69% lower), and wall time was 7.458 s versus
7.148 s (4.16% lower). Its two batches disagreed in CPU direction (-9.77% and
+7.21% reduction), including an 8.500 s candidate outlier in the first batch.
Treat that small pooled replay improvement as tentative; its cache history is
different from complete rendering. It produces no pixels.

## Win32 observations

The Win32 ABBA comparison used two complete passes per variant. Burst parsing
fell from 1,562.809 ms to 1,021.346 ms (34.65% lower); complete rendering for
those frames fell 24.69%. The complete pass's parsing phase fell 18.60%.
Renderer-thread CPU fell from 41.984 s to 37.711 s (10.18% lower). This small
Win32 batch is subject to the same timing variation; x64 remains the main focus.

## Correctness and compiled artifacts

Both architectures passed 20 contracts, both actual filter option/Main-page
checks, 54 fixture cases and all 10 extended ASS scripts. Each extended script
was compared over the full 2,428-frame timeline. The real sample passed with
256 and zero bitmap-cache entries, and through composition replay. x64 also
passed a complete diagnostic-enabled sample comparison. Native sample output
matches directly between x86 and x64. Every comparison has zero mismatches in
bitmap counts, positions, dimensions and visible bytes; IDs and pitch padding
are excluded.

VSFilter, XySubFilter, the consumer and the console application are compiled for
Win32 and x64 with matching PDBs. PE architecture was verified. Current binaries,
symbols and relevant source hashes match frozen `stable-<architecture>`
manifests, and the x64 timing executable matches the final stable executable.
All 12 build logs have no compiler/link failures or linker debug-data failures.
Builds retain version 3.2.1.900.

Outputs are in `bin/lib_18.0/x64/Release` and `bin/lib_18.0/Win32/Release`.
The [portable aggregate](benchmarks/20261026-event-spikes.json) records each pass,
mode, fixed cohort, artifact/source hashes, cache work and validation. Raw data
and symbols remain in the ignored benchmark directory. No player was launched,
and no playback settings or filter registrations were changed for this batch.

The next latency target is the remaining first-use GetSubtitle work: profile
current tag evaluation, font/style copying and word/layout construction before
broadening reuse. The initial trace includes CString CloneData work, so inspect
no-op font trimming. Bounded ahead-of-time preparation must expose its startup
or earlier-frame cost and memory use in the benchmark rather than moving work
outside the measured interval without accounting for it.
