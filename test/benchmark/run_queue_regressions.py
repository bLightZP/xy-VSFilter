"""Exercise queue policy through the built DLL, with a bounded child watchdog."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

from run_layout_regressions import remove_test_settings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--xy-dll", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    exe, dll = args.exe.resolve(strict=True), args.xy_dll.resolve(strict=True)
    result = {"exe": str(exe), "dll": str(dll),
              "dll_sha256": hashlib.sha256(dll.read_bytes()).hexdigest(), "tests": []}
    for options in (["--queue-state-contract"], ["--alpha-wrap-contract"],
                    ["--queue-filter-contract", "native", str(dll)],
                    ["--queue-filter-contract", "libass", str(dll)]):
        started = time.monotonic()
        child = subprocess.Popen([str(exe), *options], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True)
        timed_out = False
        try:
            stdout, stderr = child.communicate(timeout=45)
        except subprocess.TimeoutExpired:
            timed_out = True
            child.kill()
            stdout, stderr = child.communicate()
        finally:
            if child.poll() is not None:
                remove_test_settings(child.pid)
        passed = child.returncode == 0 and not timed_out
        result["tests"].append(dict(arguments=options, passed=passed,
                                    exit_code=child.returncode, timed_out=timed_out,
                                    seconds=time.monotonic() - started, stdout=stdout, stderr=stderr))
        print(options[:2], "PASS" if passed else "FAIL", flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    return int(not all(test["passed"] for test in result["tests"]))


if __name__ == "__main__":
    raise SystemExit(main())
