"""Summarize completed, continuous Zoom playback captures using stdlib only."""
import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path


def summarize(folder):
    metadata = json.loads((folder / "metadata.json").read_text(encoding="utf-8"))
    if metadata.get("completion") != "end_of_file" or metadata.get("error") or metadata.get("cpu_error"):
        raise ValueError(f"Incomplete or failed capture: {folder}")
    rows = list(csv.DictReader((folder / "renderer.csv").open(encoding="utf-8")))
    if any(row["counter_reset"] != "0" or not row["frames_dropped"] or not row["frames_drawn"] for row in rows):
        raise ValueError(f"Unavailable or discontinuous renderer counters: {folder}")
    if metadata.get("subtitle_index") != "0" or not metadata.get("default_subtitle"):
        raise ValueError(f"Default subtitle track was not verified: {folder}")
    if abs(int(rows[0]["position_ms"]) - 1340000) > 100 or int(rows[-1]["position_ms"]) != metadata["duration_ms"]:
        raise ValueError(f"Wrong measurement interval: {folder}")
    processes = defaultdict(list)
    for row in csv.DictReader((folder / "cpu.csv").open(encoding="utf-8")):
        processes[row["pid"]].append(row)
    cpu_seconds = sum(float(group[-1]["cpu_seconds"]) - float(group[0]["cpu_seconds"]) for group in processes.values())
    wall_seconds = metadata["capture_wall_seconds"]
    peak_cores = max(float(row["cpu_cores"]) for group in processes.values() for row in group if row["cpu_cores"])
    drops = int(rows[-1]["frames_dropped"]) - int(rows[0]["frames_dropped"])
    if drops != sum(int(row["delta_frames_dropped"] or 0) for row in rows):
        raise ValueError(f"Drop deltas disagree with endpoints: {folder}")
    return {
        "folder": str(folder),
        "startup_poll_interval_s": metadata.get("startup_poll_interval_s",1.0),
        "filter_performance_registry": metadata.get("filter_performance_registry"),
        "main_window": next(({k:v for k,v in window.items() if k != "pid"}
            for window in metadata.get("player_windows",[]) if "TMainForm" in window["class"]),None),
        "renderer": rows[0]["renderer"],
        "subtitle": metadata.get("subtitle_name"),
        "drops": drops,
        "cpu_seconds": cpu_seconds,
        "wall_seconds": wall_seconds,
        "mean_cpu_cores": cpu_seconds / wall_seconds,
        "mean_cpu_percent_machine": 100 * cpu_seconds / wall_seconds / metadata["logical_cpus"],
        "peak_single_process_cpu_cores": peak_cores,
        "peak_private_bytes": max((int(row["private_bytes"]) for group in processes.values() for row in group if row.get("private_bytes")), default=None),
        "renderer_samples": len(rows),
        "max_reply_ms": max(float(row["reply_ms"]) for row in rows),
        "xy_filter": next(f for f in metadata["registered_filters"] if f["name"] == "XySubFilter"),
        "other_filters": [f for f in metadata["registered_filters"] if f["name"] != "XySubFilter"],
        "player_sha256": metadata["player_sha256"],
        "media": metadata["media"],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folders", type=Path, nargs="+")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    results = [summarize(folder) for folder in args.folders]
    baseline = results[0]
    for result in results:
        for field in ("player_sha256", "other_filters", "renderer", "media", "subtitle", "startup_poll_interval_s"):
            if result[field] != baseline[field]:
                raise ValueError(f"Comparison changed {field}: {result['folder']}")
        result["drop_reduction_percent"] = 100 * (1 - result["drops"] / baseline["drops"]) if baseline["drops"] else None
        result["cpu_reduction_percent"] = 100 * (1 - result["cpu_seconds"] / baseline["cpu_seconds"])
    windows = [result["main_window"] for result in results if result["main_window"]]
    if windows and any(window != windows[0] for window in windows):
        raise ValueError("Benchmark window geometry or visibility changed")
    text = json.dumps(results, indent=2)
    print(text)
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
