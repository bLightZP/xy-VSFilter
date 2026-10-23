"""Temporarily select a workspace DLL, benchmark it, and restore player settings."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import time
import winreg

from run_playback_baseline import TCP


CLSID_PATH = r"Software\Classes\CLSID\{2DFCB782-EC20-4A7C-B530-4577ADB33F21}"
REG_FLAGS = winreg.KEY_READ | winreg.KEY_WRITE | winreg.KEY_WOW64_32KEY
VS_COLLECTOR = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\Team Tools\DiagnosticsHub\Collector")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile-session", type=int)
    parser.add_argument("--startup-poll-interval", type=float, default=1.0)
    parser.add_argument("--bitmap-cache-items", type=int, help="Temporary XySubFilter bitmap/group-key cache entry limit")
    parser.add_argument("--build-record", type=Path, default=Path("benchmark-results/zoom-debug-build/MAX/Debug-summary.json"))
    args = parser.parse_args()
    if not 0 < args.startup_poll_interval <= 1:
        parser.error("startup poll interval must be in (0, 1]")
    if args.bitmap_cache_items is not None and not 0 <= args.bitmap_cache_items <= 1024:
        parser.error("bitmap cache entries must be in [0, 1024]")
    args.dll = args.dll.resolve(strict=True)
    args.output = args.output.resolve()
    if args.output.exists():
        raise ValueError("Output folder must be new")
    # Avoid changing the filter or settings underneath an existing benchmark.
    try:
        connection = socket.create_connection(("127.0.0.1", 33001), timeout=.3)
    except OSError:
        pass
    else:
        connection.close()
        raise RuntimeError("TCP port 33001 is already in use")
    settings = Path(r"Z:\zpAI\zplayer.local")
    original_action = re.search(rb"(?m)^D:OPPlayComplete\|(\d+)\r?$", settings.read_bytes()).group(1)
    original_values = None
    class_existed = False
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, CLSID_PATH, 0, REG_FLAGS):
            class_existed = True
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, CLSID_PATH + r"\InprocServer32", 0, REG_FLAGS) as key:
            original_values = [winreg.EnumValue(key, i) for i in range(winreg.QueryInfoKey(key)[1])]
    except FileNotFoundError:
        pass
    performance_path = r"Software\Gabest\XySubFilter\Performance"
    performance_name = "bitmap_mru_cache_item_num"
    original_bitmap = None
    performance_existed = False
    if args.bitmap_cache_items is not None:
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,performance_path,0,REG_FLAGS) as key:
                performance_existed = True
                try:
                    value,kind = winreg.QueryValueEx(key,performance_name)
                    original_bitmap = {"value": value,"type": kind}
                except FileNotFoundError:
                    pass
        except FileNotFoundError:
            pass
    player = None
    profiling = False
    setup = {"dll": str(args.dll), "sha256": hashlib.file_digest(args.dll.open("rb"), "sha256").hexdigest(),
             "original_completion_action": original_action.decode(), "original_user_registration": original_values,
             "bitmap_cache_override": args.bitmap_cache_items, "original_bitmap_cache_value": original_bitmap}
    result = 1
    try:
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, CLSID_PATH + r"\InprocServer32", 0, REG_FLAGS) as key:
            winreg.SetValueEx(key, "", 0, winreg.REG_SZ, str(args.dll))
            winreg.SetValueEx(key, "ThreadingModel", 0, winreg.REG_SZ, "Both")
        if args.bitmap_cache_items is not None:
            with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER,performance_path,0,REG_FLAGS) as key:
                winreg.SetValueEx(key,performance_name,0,winreg.REG_DWORD,args.bitmap_cache_items)
        player = subprocess.Popen([r"Z:\zpAI\zplayer.exe", "/MultInst", "/TCP:33001"], cwd=r"Z:\zpAI")
        setup["player_pid"] = player.pid
        deadline = time.monotonic() + 30
        while True:
            try:
                tcp = TCP(33001)
                tcp.query("1000", timeout=2)
                tcp.socket.close()
                break
            except OSError:
                if player.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("Benchmark player failed to start")
                time.sleep(.2)
        if args.profile_session is not None:
            subprocess.run([str(VS_COLLECTOR / "VSDiagnostics.exe"), "start", str(args.profile_session),
                            f"/attach:{player.pid}", "/loadConfig:AgentConfigs\\CpuUsageLow.json"], cwd=VS_COLLECTOR, check=True)
            profiling = True
        command = [sys.executable, str(Path(__file__).with_name("run_playback_baseline.py")),
                   "--attach-pid", str(player.pid), "--build-record", str(args.build_record), "--output", str(args.output),
                   "--startup-poll-interval", str(args.startup_poll_interval)]
        result = subprocess.run(command).returncode
    finally:
        if profiling:
            stopped = subprocess.run([str(VS_COLLECTOR / "VSDiagnostics.exe"), "stop", str(args.profile_session),
                                     "/output:" + str(args.output / "cpu.diagsession")], cwd=VS_COLLECTOR)
            setup["profile_stop_code"] = stopped.returncode
        if player is not None and player.poll() is None:
            subprocess.run(["powershell.exe", "-NoProfile", "-Command", f"(Get-Process -Id {player.pid}).CloseMainWindow()"],
                           creationflags=subprocess.CREATE_NO_WINDOW, capture_output=True)
            try:
                player.wait(timeout=10)
            except subprocess.TimeoutExpired:
                setup["graceful_close_timed_out"] = True
                player.terminate()  # only the process this wrapper created
                player.wait(timeout=5)
        # The closed player's settings retain their original bytes except for
        # the completion action that the benchmark temporarily set to zero.
        current = settings.read_bytes()
        restored, count = re.subn(rb"(?m)^D:OPPlayComplete\|0(?=\r?$)",
                                 b"D:OPPlayComplete|" + original_action, current)
        if count:
            settings.write_bytes(restored)
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, CLSID_PATH + r"\InprocServer32", 0, REG_FLAGS) as key:
            if winreg.QueryValueEx(key, "")[0] != str(args.dll):
                raise RuntimeError("Filter registration changed during the test; refusing to overwrite it")
            for name in ("", "ThreadingModel"):
                winreg.DeleteValue(key, name)
            if original_values is not None:
                for name, value, kind in original_values:
                    winreg.SetValueEx(key, name, 0, kind, value)
        if original_values is None:
            winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, CLSID_PATH + r"\InprocServer32", winreg.KEY_WOW64_32KEY)
        if not class_existed:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, CLSID_PATH, 0, REG_FLAGS) as key:
                empty = winreg.QueryInfoKey(key)[:2] == (0, 0)
            if empty:
                winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, CLSID_PATH, winreg.KEY_WOW64_32KEY)
        setup["registration_restored"] = True
        if args.bitmap_cache_items is not None:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,performance_path,0,REG_FLAGS) as key:
                if winreg.QueryValueEx(key,performance_name)[0] != args.bitmap_cache_items:
                    raise RuntimeError("Bitmap cache setting changed during the benchmark; refusing to overwrite it")
                if original_bitmap is None:
                    winreg.DeleteValue(key,performance_name)
                else:
                    winreg.SetValueEx(key,performance_name,0,original_bitmap["type"],original_bitmap["value"])
                empty = winreg.QueryInfoKey(key)[:2] == (0,0)
            if not performance_existed and empty:
                winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER,performance_path,winreg.KEY_WOW64_32KEY)
            setup["bitmap_cache_restored"] = True
        if args.output.exists():
            (args.output / "filter-test-setup.json").write_text(json.dumps(setup, indent=2), encoding="utf-8")
    raise SystemExit(result)


if __name__ == "__main__":
    main()
