"""Compare all source-frame subtitle bitmap hashes in the ending workload."""
import argparse
import csv
import json
from pathlib import Path


def load(path):
    rows = list(csv.DictReader(path.open(encoding="utf-8")))
    # Older control captures may include an additional empty frame after EOF.
    rows = [row for row in rows if 13400000000 <= int(row["time_100ns"]) < 14412720000]
    if [int(row["frame"]) for row in rows] != list(range(32128, 34556)):
        raise ValueError(f"Missing, repeated, or unexpected source frames: {path}")
    times = sorted(float(row["render_ms"]) for row in rows)
    return rows, {
        "frames": len(rows),
        "nonempty_frames": sum(int(row["bitmaps"]) > 0 for row in rows),
        "render_ms_mean": sum(times) / len(times),
        "render_ms_p95": times[int(0.95 * (len(times) - 1))],
        "render_ms_p99": times[int(0.99 * (len(times) - 1))],
        "render_ms_max": max(times),
        "over_41_708ms": sum(time > 1001 / 24 for time in times),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("control", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    control, control_summary = load(args.control)
    candidate, candidate_summary = load(args.candidate)
    fields = ("frame", "time_100ns", "bitmaps", "sha256")
    mismatches = [a["frame"] for a, b in zip(control, candidate) if any(a[field] != b[field] for field in fields)]
    result = {"frames_compared": len(control), "mismatch_count": len(mismatches), "mismatch_examples": mismatches[:20],
              "control": control_summary, "candidate": candidate_summary,
              "timing_note": "Offline wall times include other activity; playback captures measure end-to-end CPU independently."}
    text = json.dumps(result, indent=2)
    print(text)
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")
    raise SystemExit(bool(mismatches))


if __name__ == "__main__":
    main()
