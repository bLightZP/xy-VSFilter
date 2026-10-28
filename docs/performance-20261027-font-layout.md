# Native font/style and word/layout work, 2026-10-27

This batch started before midnight in `benchmark-results/font-layout-20261026/`.
It uses the console renderer only: one render thread, logical CPU 7, affinity
0x80 and verified Above Normal priority. Builds are serial on CPU 7 at Normal
priority and do not overlap timed runs. The control is the preceding validated
command-trie/inline-parameter build. No playback is run.

## Evidence and change

The unchanged x64 console profile collected 3,872 CPU samples, 3,857 with stacks,
with zero lost events. GetSubtitle appeared in 555 samples; string CloneData
work appeared under GetSubtitle in 37 samples. Source-line lookup points most
of those callers to the source-string copy at RTS.cpp:3186 in the control.
CloneData includes ordinary copy-on-write sharing/refcount work and does not
by itself establish a fresh character-buffer allocation. The installed CString
implementation also confirms that Trim does not clone an unchanged font name.
A no-op-trim workaround was therefore not added.

GetSubtitle now checks static cached states before reading/copying text. Its
small reconstruction-hit helper borrows Unicode entry text while the entry is
stable; ANSI uses the same GetStrW conversion and a temporary alive through the
call. CreateSubtitle contains the large construction path and retains existing
owned source text for cached states. Cache interval, delay and source-identity
checks are unchanged, as are expiry, seeking and invalidation rules.

Working and original styles are copy-constructed directly from the selected
style and its PAR-adjusted result. This avoids two default-style initialization
sequences and replacement assignments on each new state. The PAR adjustment
order and source style values remain unchanged.

MakeLines handles the common one-word case directly when that word is neither
whitespace nor a line break and there is no existing line. The general path
always accepts its first word, even beyond wrap width, and no trimming/merging
is possible in this case. The fast path retains that rule, zero-based metric
maxima, exact outline rounding and the shared final rectangle calculation.
Other cases use the existing wrapping and compaction. Read-only traversals and
line appends avoid temporary shared-pointer copies; each line keeps ownership.

The new font/layout contract checks Unicode/ANSI conversion lifetimes, cached
versus uncached output, all PAR modes at two scale factors, forced defaults,
style reset/source edits and non-mutation of selected styles. Two fixtures add
single words across wrap modes/alignments, large/tiny text, whitespace/breaks,
NBSP, multiline text, named styles, font-name whitespace and polygons, plus an
ANSI file. Their legacy controls come from the preserved old executables.

## Method

Timing uses the default extracted ASS track and private fonts, 1920x1080,
24000/1001 fps, source frames 32128..34555 (22:20 through EOF), with 256 bitmap
cache entries. Fresh processes run serial control/candidate/candidate/control
batches. Full rendering is primary; isolated ParseScript has a separate cache
history and produces no pixels. The fixed 17-frame burst cohort is carried
forward from source event-start timestamps, independent of new timings.
Nested phase counters overlap; thread CPU is recorded separately. Fixed
priority/affinity do not eliminate frequency, cache and memory interference.

Final results and both-architecture pixel/build checks are recorded below.

## x64 timing results

Both ABBA batches passed all frame hashes. Across four complete passes per
variant, median renderer-thread CPU fell from 30.234 s to 29.828 s (1.34% lower),
and wall time from 30.326 s to 29.562 s (2.52% lower). Parsing within complete
rendering fell from 5.314 s to 4.529 s (14.77% lower). The individual batches
showed CPU reductions of 3.41% and 1.09%; the modest overall gain remains subject
to scheduling/frequency/cache variation.

The fixed 17-frame cohort's parsing total fell from 776.100 ms to 714.564 ms
(7.93% lower); complete rendering for those frames fell 4.05%. Median per-pass
wall p95/p99 fell from 28.497/58.210 ms to 27.849/55.915 ms. The median maximum
fell from 126.477 to 120.517 ms. Some native frames still exceed the 41.708 ms
source-frame budget; no zero-drop playback claim is made.

Isolated ParseScript replay separately measured 6.180 s versus 5.195 s of thread
CPU (15.93% lower), and 6.166 s versus 5.205 s wall time (15.58% lower), two passes
per variant. Its cache history differs from complete rendering.

Syntax-cache work counts match between control and candidate, supporting that
these changes remove copying/assembly overhead rather than change the work
selected for rendering. The syntax/reconstruction/bitmap capacities are unchanged.
Median whole-process peak commit was 441,063,424 versus 443,033,600 bytes (about
1.9 MiB higher); this includes source, font and image-cache storage and is not
an isolated measure of these source changes.

## Win32 and final correctness

The Win32 ABBA comparison used two complete passes per variant. Median complete
renderer-thread CPU fell from 37.852 s to 36.672 s (3.12% lower). The parsing
phase fell from 7.007 s to 6.133 s (12.48% lower). The fixed burst cohort's
parsing time fell 4.28% and its complete rendering time fell 2.51%. These are
observations from this small batch, not guaranteed playback improvements.

Both architectures passed 21 contracts, both actual filter option/Main-page
checks, 54 fixture cases and 12 extended scripts. Each script was checked over
the complete 2,428-frame timeline. The sample matches its original pixel control
with 256 and zero bitmap-cache entries and through composition replay. x64 also
passed a complete diagnostic-enabled sample pass. The native sample matches
directly between x86 and x64. Every comparison has zero mismatches in bitmap
counts, positions, dimensions and visible bytes; cache IDs and pitch padding
are excluded.

VSFilter, XySubFilter, the consumer and the console application are rebuilt for
both architectures with matching PDBs. PE architecture, binary/source/symbol
hashes and the final x64 timing executable were verified against preserved
snapshots. All ten build logs have no compiler/link failures or linker debug-data
failures. Version remains 3.2.1.900. Outputs are under
`bin/lib_18.0/x64/Release` and `bin/lib_18.0/Win32/Release`.

The [portable aggregate](benchmarks/20261027-font-layout.json) records per-pass
results, scheduling, source/artifact hashes, fixed burst frames and verification.
Raw records and matching symbols remain in the ignored benchmark directory.
No player was launched and no playback settings or filter registrations were
changed for this batch.

Further first-use latency remains. Reprofile the current construction/evaluation
path before extending style/layout lifetime reuse, and preserve all cache,
source-encoding, geometry, font and ownership invalidation conditions. Any
preparation moved into loading or earlier frames must account for that work and
memory explicitly.
