"""Bounded native font and EOF/seek queue regressions against actual DLLs."""
import argparse
import json
from pathlib import Path
import subprocess
import time

from run_layout_regressions import remove_test_settings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--xy-dll", type=Path, required=True)
    parser.add_argument("--vs-dll", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    fixtures = Path(__file__).resolve().parent / "fixtures"
    font = str(fixtures / "fonts/KaushanScript-Regular.otf")
    script = str(fixtures / "large-native-font.ass")
    tests = [
        ["--font-size-contract", font],
        ["--queue-state-contract"],
        ["--font-size-filter-contract", "xy", str(args.xy_dll.resolve()), script, font],
        ["--font-size-filter-contract", "vs", str(args.vs_dll.resolve()), script, font],
        ["--queue-filter-contract", "native", str(args.xy_dll.resolve())],
        ["--queue-filter-contract", "libass", str(args.xy_dll.resolve())],
    ]
    results = []
    for options in tests:
        start = time.monotonic()
        child = subprocess.Popen([str(args.exe.resolve()), *options],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        timed_out = False
        try:
            stdout, stderr = child.communicate(timeout=90)
        except subprocess.TimeoutExpired:
            timed_out = True
            child.kill()
            stdout, stderr = child.communicate()
        finally:
            remove_test_settings(child.pid)
        results.append(dict(arguments=options, exit_code=child.returncode, timed_out=timed_out,
                            seconds=time.monotonic()-start, stdout=stdout, stderr=stderr))
        print(options[:2], "PASS" if child.returncode == 0 and not timed_out else "FAIL", flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(results, indent=2), encoding="utf-8")
    return int(any(row["exit_code"] != 0 or row["timed_out"] for row in results))


if __name__ == "__main__":
    raise SystemExit(main())
