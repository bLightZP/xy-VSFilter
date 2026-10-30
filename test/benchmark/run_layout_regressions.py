"""Run the layout contracts with a watchdog and isolated-settings cleanup."""
import argparse
import json
from pathlib import Path
import subprocess
import time
import winreg


def remove_test_settings(pid):
    # This is the dedicated subtree created by LayoutFilterContract, never the
    # user's Gabest settings. The child has exited before cleanup is attempted.
    root = rf"Software\xySubFilterLayoutTests\layout-{pid}"

    def remove(path):
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_ALL_ACCESS) as key:
                children = [winreg.EnumKey(key, i) for i in range(winreg.QueryInfoKey(key)[0])]
            for child in children:
                remove(path + "\\" + child)
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)
        except FileNotFoundError:
            pass

    remove(root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--xy-dll", type=Path, required=True)
    parser.add_argument("--vs-dll", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    tests = [
        ["--layout-contract"],
        ["--layout-filter-contract", "xy", str(args.xy_dll.resolve())],
        ["--layout-filter-contract", "vs", str(args.vs_dll.resolve())],
    ]
    results = []
    for options in tests:
        started = time.monotonic()
        child = subprocess.Popen([str(args.exe.resolve()), *options],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        timed_out = False
        try:
            stdout, stderr = child.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            timed_out = True
            child.kill()
            stdout, stderr = child.communicate()
        finally:
            if child.poll() is not None and options[0] == "--layout-filter-contract":
                remove_test_settings(child.pid)
        results.append(dict(arguments=options, exit_code=child.returncode,
                            timed_out=timed_out, seconds=time.monotonic() - started,
                            stdout=stdout, stderr=stderr))
        print(options[:2], "PASS" if child.returncode == 0 and not timed_out else "FAIL", flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(results, indent=2), encoding="utf-8")
    return int(any(row["exit_code"] != 0 or row["timed_out"] for row in results))


if __name__ == "__main__":
    raise SystemExit(main())
