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

from run_playback_baseline import TCP, pe_machine


CLSID_PATH = r"Software\Classes\CLSID\{2DFCB782-EC20-4A7C-B530-4577ADB33F21}"
VS_COLLECTOR = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\Team Tools\DiagnosticsHub\Collector")


def player_options(data, values):
    for name, value in values.items():
        pattern = rb"(?m)^" + re.escape(name) + rb"\|([^\r\n]*)(?=\r?$)"
        if value is None:
            data = re.sub(pattern + rb"(?:\r?\n)?", b"", data)
        elif re.search(pattern, data):
            data = re.sub(pattern, lambda match: name + b"|" + value, data)
        else:
            data += (b"" if data.endswith(b"\n") else b"\r\n") + name + b"|" + value + b"\r\n"
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--engine", choices=("internal32", "external32", "external64"), default="internal32")
    parser.add_argument("--renderer", choices=("current", "madvr"), default="current")
    parser.add_argument("--media", type=Path)
    parser.add_argument("--seek-seconds", type=int, default=1340)
    parser.add_argument("--paused-seconds", type=int, default=0,
                        help="Hold paused for queue inspection before measured playback")
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
    bits = 64 if args.engine == "external64" else 32
    reg_view = winreg.KEY_WOW64_64KEY if bits == 64 else winreg.KEY_WOW64_32KEY
    reg_flags = winreg.KEY_READ | winreg.KEY_WRITE | reg_view
    if pe_machine(args.dll) != (0x8664 if bits == 64 else 0x14c):
        parser.error("DLL architecture does not match the selected engine")
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
    engine_options = {b"D:OPExternalMediaEngineEnable": b"0" if args.engine == "internal32" else b"1"}
    if args.renderer == "madvr":
        engine_options[b"D:OPCMDefRenderer"] = b"8"
    if args.engine != "internal32":
        engine_options.update({b"D:OPExternalMediaEngineMode": b"1" if bits == 64 else b"0",
                               b"S:OPExternalMediaEngineExtList": b"MKV;MP4",
                               b"D:OPEnableVis": b"0", b"D:OPEnableVisVid": b"0"})
    original_engine_options = {}
    for name in engine_options:
        match = re.search(rb"(?m)^" + re.escape(name) + rb"\|([^\r\n]*)(?=\r?$)", settings.read_bytes())
        original_engine_options[name] = match.group(1) if match else None
    original_action = re.search(rb"(?m)^D:OPPlayComplete\|(\d+)\r?$", settings.read_bytes()).group(1)
    original_values = None
    class_existed = False
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, CLSID_PATH, 0, reg_flags):
            class_existed = True
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, CLSID_PATH + r"\InprocServer32", 0, reg_flags) as key:
            original_values = [winreg.EnumValue(key, i) for i in range(winreg.QueryInfoKey(key)[1])]
    except FileNotFoundError:
        pass
    performance_path = r"Software\Gabest\XySubFilter\Performance"
    performance_name = "bitmap_mru_cache_item_num"
    original_bitmap = None
    performance_existed = False
    if args.bitmap_cache_items is not None:
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,performance_path,0,reg_flags) as key:
                performance_existed = True
                try:
                    value,kind = winreg.QueryValueEx(key,performance_name)
                    original_bitmap = {"value": value,"type": kind}
                except FileNotFoundError:
                    pass
        except FileNotFoundError:
            pass
    player = None
    setup = {"dll": str(args.dll), "sha256": hashlib.file_digest(args.dll.open("rb"), "sha256").hexdigest(),
             "original_completion_action": original_action.decode(), "original_user_registration": original_values,
             "engine": args.engine, "filter_bitness": bits,
             "original_engine_options": {name.decode(): value.decode() if value is not None else None for name,value in original_engine_options.items()},
             "bitmap_cache_override": args.bitmap_cache_items, "original_bitmap_cache_value": original_bitmap}
    result = 1
    try:
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, CLSID_PATH + r"\InprocServer32", 0, reg_flags) as key:
            winreg.SetValueEx(key, "", 0, winreg.REG_SZ, str(args.dll))
            winreg.SetValueEx(key, "ThreadingModel", 0, winreg.REG_SZ, "Both")
        if args.bitmap_cache_items is not None:
            with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER,performance_path,0,reg_flags) as key:
                winreg.SetValueEx(key,performance_name,0,winreg.REG_DWORD,args.bitmap_cache_items)
        settings.write_bytes(player_options(settings.read_bytes(), engine_options))
        player = subprocess.Popen([r"Z:\zpAI\zplayer.exe", "/MultInst", "/TCP:33001"], cwd=r"Z:\zpAI")
        setup["player_pid"] = player.pid
        command = [sys.executable, str(Path(__file__).with_name("run_playback_baseline.py")),
                   "--attach-pid", str(player.pid), "--build-record", str(args.build_record), "--output", str(args.output),
                   "--startup-poll-interval", str(args.startup_poll_interval),
                   "--seek-seconds", str(args.seek_seconds), "--paused-seconds", str(args.paused_seconds),
                   "--engine", args.engine, "--expected-filter", str(args.dll)]
        if args.media is not None:
            command += ["--media", str(args.media.resolve(strict=True))]
        if args.renderer == "madvr":
            command += ["--expected-renderer", "madVR"]
        if args.profile_session is not None:
            command += ["--profile-session",str(args.profile_session)]
        result = subprocess.run(command).returncode
    finally:
        if player is not None:
            setup["player_running_before_cleanup"] = player.poll() is None
            setup["player_exit_code_before_cleanup"] = player.poll()
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
        settings.write_bytes(player_options(restored, original_engine_options))
        setup["engine_options_restored"] = True
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, CLSID_PATH + r"\InprocServer32", 0, reg_flags) as key:
            if winreg.QueryValueEx(key, "")[0] != str(args.dll):
                raise RuntimeError("Filter registration changed during the test; refusing to overwrite it")
            for name in ("", "ThreadingModel"):
                winreg.DeleteValue(key, name)
            if original_values is not None:
                for name, value, kind in original_values:
                    winreg.SetValueEx(key, name, 0, kind, value)
        if original_values is None:
            winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, CLSID_PATH + r"\InprocServer32", reg_view)
        if not class_existed:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, CLSID_PATH, 0, reg_flags) as key:
                empty = winreg.QueryInfoKey(key)[:2] == (0, 0)
            if empty:
                winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, CLSID_PATH, reg_view)
        setup["registration_restored"] = True
        if args.bitmap_cache_items is not None:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,performance_path,0,reg_flags) as key:
                if winreg.QueryValueEx(key,performance_name)[0] != args.bitmap_cache_items:
                    raise RuntimeError("Bitmap cache setting changed during the benchmark; refusing to overwrite it")
                if original_bitmap is None:
                    winreg.DeleteValue(key,performance_name)
                else:
                    winreg.SetValueEx(key,performance_name,0,original_bitmap["type"],original_bitmap["value"])
                empty = winreg.QueryInfoKey(key)[:2] == (0,0)
            if not performance_existed and empty:
                winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER,performance_path,reg_view)
            setup["bitmap_cache_restored"] = True
        if args.output.exists():
            (args.output / "filter-test-setup.json").write_text(json.dumps(setup, indent=2), encoding="utf-8")
    raise SystemExit(result)


if __name__ == "__main__":
    main()
