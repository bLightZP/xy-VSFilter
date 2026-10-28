"""Compare fixed-work native phase CSVs; timings are diagnostic, not playback CPU."""
import argparse
import csv
import json
from pathlib import Path

from compare_rendered import load


PHASES = ("render_core_ms", "parse_ms", "build_draw_ms", "composite_ms",
          "tree_ms", "merge_ms", "group_ms", "bitmap_ms")
WORK = ("input_items", "groups", "member_references", "unique_members", "draw_items")
XY = ("xy_alloc_calls", "xy_free_calls", "xy_requested_bytes", "xy_failures")


def summarize(path):
    # Reuse the source-frame completeness check, then read diagnostics explicitly.
    load(path)
    with path.open(encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    for row in rows:
        if any(float(row[key]) < 0 for key in (*PHASES, *WORK, *XY, "draw_vector_growths", "xy_peak_above_start")):
            raise ValueError(f"Negative diagnostic value: {path}")
        if int(row["unique_members"]) > int(row["member_references"]):
            raise ValueError(f"Unique memberships exceed references: {path}")
        if int(row["xy_failures"]):
            raise ValueError(f"xy_malloc failed during rendering: {path}")
    return rows, {
        "phase_totals_ms": {key: sum(float(row[key]) for row in rows) for key in PHASES},
        "work_totals": {key: sum(int(row[key]) for row in rows) for key in WORK},
        "draw_append_capacity_changes": sum(int(row["draw_vector_growths"]) for row in rows),
        "xy_allocator_totals": {key: sum(int(row[key]) for row in rows) for key in XY},
        "xy_peak_above_frame_start_bytes": max(int(row["xy_peak_above_start"]) for row in rows),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("control", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    control_rows, control = summarize(args.control)
    candidate_rows, candidate = summarize(args.candidate)
    pixel_fields = ("frame", "time_100ns", "bitmaps", "sha256")
    pixel_mismatches = sum(any(a[key] != b[key] for key in pixel_fields)
                           for a, b in zip(control_rows, candidate_rows))
    work_mismatches = sum(any(a[key] != b[key] for key in WORK)
                          for a, b in zip(control_rows, candidate_rows))
    result = {
        "frames": len(control_rows),
        "pixel_mismatches": pixel_mismatches,
        "work_count_mismatches": work_mismatches,
        "control": control,
        "candidate": candidate,
        "notes": [
            "Composite/render-core phases contain nested phases; do not add them together.",
            "Draw capacity changes count AddTail growth; pre-reservation allocations are excluded.",
            "xy_malloc counts exclude ATL/STL/shared_ptr heap allocations.",
            "Single-pass wall times do not establish a playback speed-up.",
        ],
    }
    text = json.dumps(result, indent=2)
    print(text)
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")
    raise SystemExit(bool(pixel_mismatches or work_mismatches))


if __name__ == "__main__":
    main()
