"""Reanalyze saved native timings and source ASS; does not render or change code.

Run with the existing Python from any directory. Inputs under benchmark-results
are local/ignored artifacts. The exact-pattern census is deliberately not an ASS
interpreter: renderer behavior is established by reviewing RTS.cpp separately.
"""

import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import re
import statistics


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def timestamp_ms(value):
    hours, minutes, seconds = value.split(":")
    whole, fraction = seconds.split(".")
    return ((int(hours) * 60 + int(minutes)) * 60 + int(whole)) * 1000 + int(fraction.ljust(3, "0"))


# This exact family is counted to establish workload coverage, never to propose
# filename/style/shape-specific behavior in the renderer.
NESTED_SQUARE = re.compile(
    r"^\{\\an5\\blur0\\bord0\\shad0\.01\\1a&HFE&\\3a&HFE&"
    r"\\4c&H000000&\\pos\([^)]*\)\\t\((\d+),(\d+),\\4a&H([0-9A-F]+)&"
    r"\\p1\\t\((\d+),(\d+),\\4a&H00&\)\\clip\(0,0,1920,81\)\}"
    r"m 0 0 l 15 0 15 15 0 15$"
)
SQUARE_TEXT = "m 0 0 l 15 0 15 15 0 15"
METRICS = [
    "render_ms", "parse_ms", "build_draw_ms", "composite_ms", "tree_ms",
    "merge_ms", "group_ms", "bitmap_ms", "input_items", "groups",
    "member_references", "unique_members", "draw_items", "xy_alloc_calls",
]


def census(events):
    shapes = collections.Counter(re.sub(r"\{[^}]*\}", "", e["text"]) for e in events)
    nested = [NESTED_SQUARE.fullmatch(e["text"]) for e in events]
    nested = [m for m in nested if m]
    return {
        "events": len(events),
        "styles": dict(collections.Counter(e["style"] for e in events)),
        "source_characters": sum(len(e["text"]) for e in events),
        "unique_literal_spans_after_brace_removal": len(shapes),
        "square_literal_events": shapes[SQUARE_TEXT],
        "exact_nested_square_programs": len(nested),
        "nested_square_time_pairs_equal": all(m[1] == m[2] and m[4] == m[5] for m in nested),
        "literal_transform_events": sum("\\t(" in e["text"] for e in events),
        "literal_rect_or_vector_clip_events": sum("\\clip(" in e["text"] for e in events),
        "literal_inverse_clip_events": sum("\\iclip(" in e["text"] for e in events),
        "source_longer_than_8192": [{"line": e["line"], "characters": len(e["text"])} for e in events if len(e["text"]) > 8192],
        "first_line": min((e["line"] for e in events), default=None),
        "last_line": max((e["line"] for e in events), default=None),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.output or root / "docs/benchmarks/20261027-spike-followup-analysis.json"
    manifest_path = root / "docs/benchmarks/20261027-spike-paths.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    ass = root / "benchmark-results/media/default.ass"
    assert sha256(ass) == manifest["sample"]["ass_sha256"], "ASS differs from measured input"
    inputs = [{"path": str(p.relative_to(root)), "sha256": sha256(p)} for p in [manifest_path, ass]]
    events = []
    for line_number, line in enumerate(ass.read_text(encoding="utf-8-sig").splitlines(), 1):
        if line.startswith("Dialogue:"):
            fields = line.split(",", 9)
            events.append({"line": line_number, "start": timestamp_ms(fields[1]),
                           "end": timestamp_ms(fields[2]), "style": fields[3], "text": fields[9]})
    window = [e for e in events if e["start"] < 1441272 and e["end"] > 1340000]
    frames = [32249, 32693, 32780, 32781, 32782, 32854, 33157, 33159, 33448, 33518, 33973, 34048]
    source_frames = []
    for frame in frames:
        # Match the console's integer midpoint in 100 ns units, with integer
        # comparisons against centisecond source times.
        tick = (2 * frame + 1) * 1001 * 10000000 // 48000
        previous = (2 * frame - 1) * 1001 * 10000000 // 48000
        active = [e for e in events if e["start"] * 10000 <= tick < e["end"] * 10000]
        new = [e for e in active if e["start"] * 10000 > previous]
        source_frames.append({"frame": frame, "time_ms": tick / 10000,
                              "active": census(active), "new": census(new)})

    base = root / "benchmark-results/spike-paths-20261027"
    architectures = {}
    for arch in ["x64", "Win32"]:
        paths = sorted(p for folder in [f"abba-g-{arch}", f"abba-g-{arch}-repeat"]
                       for p in (base / folder).glob("*-candidate.csv"))
        assert len(paths) == 4
        runs = []
        for path in paths:
            inputs.append({"path": str(path.relative_to(root)), "sha256": sha256(path)})
            with path.open(newline="") as stream:
                runs.append({int(r["frame"]): r for r in csv.DictReader(stream)})
        assert all(set(r) == set(runs[0]) for r in runs)
        assert all(len({r[f]["sha256"] for r in runs}) == 1 for f in runs[0])
        medians = [{"frame": f, "time_ms": int(runs[0][f]["time_100ns"]) / 10000,
                    **{k: statistics.median(float(r[f][k]) for r in runs) for k in METRICS}}
                   for f in sorted(runs[0])]
        first = list(runs[0].values())
        repeated = [b for a, b in zip(first, first[1:]) if a["sha256"] == b["sha256"] and int(b["bitmaps"]) > 0]
        counter_keys = ["input_items", "groups", "member_references", "unique_members", "draw_items"]
        same_counters = [b for a, b in zip(first, first[1:]) if int(b["bitmaps"]) > 0 and all(a[k] == b[k] for k in counter_keys)]
        mismatches = []
        for record in manifest["architectures"][arch]["sources"]:
            path = root / record["path"]
            if not path.exists() or sha256(path) != record["sha256"]:
                mismatches.append(record["path"])
        architectures[arch] = {
            "candidate_passes": len(runs), "frames": len(medians),
            "recorded_sources_checked": len(manifest["architectures"][arch]["sources"]),
            "recorded_source_mismatches": mismatches,
            "frames_over_source_period": sum(r["render_ms"] > 1001 / 24 for r in medians),
            "top_20_by_median_render_ms": sorted(medians, key=lambda r: r["render_ms"], reverse=True)[:20],
            "selected_frames": [r for r in medians if r["frame"] in frames],
            "first_pass_adjacent_identical_nonempty_hashes": {
                "count": len(repeated), "render_ms_sum": sum(float(r["render_ms"]) for r in repeated),
                "render_ms_max": max(float(r["render_ms"]) for r in repeated)},
            "first_pass_adjacent_equal_group_counters": len(same_counters),
            "counter_equality_is_not_topology_equality": True,
        }
    result = {
        "scope": "Reanalysis of saved candidate ABBA CSVs and source text; no new timing run or renderer emulation",
        "frame_indexing": "zero based; midpoint; 24000/1001 fps",
        "timing_rule": "Per-frame, per-column median of four passes; phases overlap; medians need not add",
        "inputs": inputs, "ass_dialogue_events": len(events), "window_2220_to_2401_272": census(window),
        "selected_source_frames": source_frames, "architectures": architectures,
    }
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Saved {output}")
    print(f"Window: {len(window)} events; nested rectangle family: {census(window)['exact_nested_square_programs']}")
    for arch, data in architectures.items():
        print(f"{arch}: {data['candidate_passes']} passes; {data['recorded_sources_checked']} source hashes; mismatches: {data['recorded_source_mismatches']}")


if __name__ == "__main__":
    main()
