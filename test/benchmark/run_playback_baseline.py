"""Capture the sample ending in Zoom Player, with renderer and Windows CPU data."""

import argparse
import csv
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
import os
import re
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time
from types import SimpleNamespace
import winreg

from collect_renderer_stats import counter_deltas, parse_stats


def pe_machine(path):
    with Path(path).open("rb") as stream:
        stream.seek(60)
        offset = struct.unpack("<I", stream.read(4))[0]
        stream.seek(offset)
        if stream.read(4) != b"PE\0\0":
            raise ValueError("Not a PE executable: " + str(path))
        return struct.unpack("<H", stream.read(2))[0]


class TCP:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.buffer = b""
        self.messages = []

    def send(self, command):
        self.socket.sendall((command + "\r\n").encode("utf-8"))

    def query(self, code, timeout=30, data=None):
        self.send(code + (" " + data if data is not None else ""))
        deadline = time.monotonic() + timeout
        while True:
            while b"\r\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\r\n", 1)
                line = raw.decode("utf-8", errors="replace")
                self.messages.append({"elapsed_s": time.monotonic(), "message": line})
                if line.startswith(code + " "):
                    return line[5:]
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("No reply for " + code)
            self.socket.settimeout(remaining)
            data = self.socket.recv(65536)
            if not data:
                raise ConnectionError("Player closed TCP connection")
            self.buffer += data

    def stats(self):
        return parse_stats("1150 " + self.query("1150"))


class ProcessEntry(c.Structure):
    _fields_ = [
        ("dwSize", w.DWORD), ("cntUsage", w.DWORD), ("pid", w.DWORD),
        ("heap", c.c_size_t), ("module", w.DWORD), ("threads", w.DWORD),
        ("parent", w.DWORD), ("priority", w.LONG), ("flags", w.DWORD),
        ("name", w.WCHAR * 260),
    ]


class ProcessMemory(c.Structure):
    _fields_ = [("cb", w.DWORD), ("PageFaultCount", w.DWORD)] + [
        (name, c.c_size_t) for name in (
            "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage",
            "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")
    ]


class CpuCapture:
    def __init__(self, pid, output):
        self.pid, self.output = pid, output
        self.stop = threading.Event()
        self.ready = threading.Event()
        self.thread = threading.Thread(target=self.run)
        self.error = None
        self.kernel = c.WinDLL("kernel32", use_last_error=True)
        self.kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
        self.kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
        self.kernel.Process32FirstW.argtypes = [w.HANDLE, c.POINTER(ProcessEntry)]
        self.kernel.Process32NextW.argtypes = [w.HANDLE, c.POINTER(ProcessEntry)]
        self.kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.kernel.OpenProcess.restype = w.HANDLE
        self.kernel.GetProcessTimes.argtypes = [w.HANDLE] + [c.POINTER(w.FILETIME)] * 4
        self.kernel.K32GetProcessMemoryInfo.argtypes = [w.HANDLE, c.POINTER(ProcessMemory), w.DWORD]
        self.kernel.CloseHandle.argtypes = [w.HANDLE]

    def processes(self):
        snapshot = self.kernel.CreateToolhelp32Snapshot(2, 0)
        if snapshot == c.c_void_p(-1).value:
            raise c.WinError(c.get_last_error())
        try:
            entry = ProcessEntry()
            entry.dwSize = c.sizeof(entry)
            entries = []
            valid = self.kernel.Process32FirstW(snapshot, c.byref(entry))
            while valid:
                entries.append((entry.pid, entry.parent, entry.name.lower()))
                valid = self.kernel.Process32NextW(snapshot, c.byref(entry))
            selected = {self.pid: "zplayer.exe"}
            for _ in range(3):
                for pid, parent, name in entries:
                    if parent in selected and name in ("zpmediaengine.exe", "zpmediaengine64.exe"):
                        selected[pid] = name
            return selected
        finally:
            self.kernel.CloseHandle(snapshot)

    def cpu_time(self, pid):
        handle = self.kernel.OpenProcess(0x1000, False, pid)
        if not handle:
            raise c.WinError(c.get_last_error())
        try:
            times = [w.FILETIME() for _ in range(4)]
            if not self.kernel.GetProcessTimes(handle, *(c.byref(t) for t in times)):
                raise c.WinError(c.get_last_error())
            memory = ProcessMemory()
            memory.cb = c.sizeof(memory)
            if not self.kernel.K32GetProcessMemoryInfo(handle, c.byref(memory), memory.cb):
                raise c.WinError(c.get_last_error())
            return (sum((t.dwHighDateTime << 32) | t.dwLowDateTime for t in times[2:]) / 1e7,
                    memory.PrivateUsage, memory.WorkingSetSize)
        finally:
            self.kernel.CloseHandle(handle)

    def run(self):
        try:
            with self.output.open("x", newline="", encoding="utf-8") as handle:
                writer = csv.DictWriter(handle, fieldnames=[
                    "utc", "monotonic_s", "pid", "process", "cpu_seconds",
                    "interval_s", "cpu_cores", "cpu_percent_machine", "logical_cpus",
                    "private_bytes", "working_set_bytes",
                ])
                writer.writeheader()
                previous = {}
                next_poll = time.monotonic()
                while True:
                    now = time.monotonic()
                    for pid, name in self.processes().items():
                        cpu, private_bytes, working_set_bytes = self.cpu_time(pid)
                        before = previous.get(pid)
                        elapsed = now - before[0] if before else None
                        cores = (cpu - before[1]) / elapsed if before and elapsed > 0 else None
                        writer.writerow({
                            "utc": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
                            "monotonic_s": f"{now:.6f}", "pid": pid, "process": name,
                            "cpu_seconds": f"{cpu:.7f}",
                            "interval_s": f"{elapsed:.6f}" if elapsed is not None else None,
                            "cpu_cores": f"{cores:.6f}" if cores is not None else None,
                            "cpu_percent_machine": f"{cores / os.cpu_count() * 100:.6f}" if cores is not None else None,
                            "logical_cpus": os.cpu_count(),
                            "private_bytes": private_bytes, "working_set_bytes": working_set_bytes,
                        })
                        previous[pid] = (now, cpu)
                    handle.flush()
                    self.ready.set()
                    next_poll += 1
                    if self.stop.wait(max(0, next_poll - time.monotonic())):
                        break
        except Exception as error:
            self.error = repr(error)
            self.ready.set()


def powershell_json(command):
    text = subprocess.check_output(
        ["powershell.exe", "-NoProfile", "-Command", command + " | ConvertTo-Json -Depth 4 -Compress"],
        text=True, creationflags=subprocess.CREATE_NO_WINDOW,
    ).strip()
    return json.loads(text) if text else None


def window_snapshot(pids):
    """Record visible benchmark windows without changing focus or geometry."""
    user = c.WinDLL("user32", use_last_error=True)
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback_type, w.LPARAM]
    user.EnumWindows.restype = w.BOOL
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.IsIconic.argtypes = [w.HWND]
    user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    windows = []
    @callback_type
    def visit(handle, _):
        pid = w.DWORD()
        user.GetWindowThreadProcessId(handle, c.byref(pid))
        if pid.value in pids and user.IsWindowVisible(handle):
            rect, client = w.RECT(), w.RECT()
            name = c.create_unicode_buffer(256)
            if user.GetWindowRect(handle,c.byref(rect)) and user.GetClientRect(handle,c.byref(client)):
                user.GetClassNameW(handle,name,len(name))
                windows.append({"pid": pid.value, "class": name.value, "minimized": bool(user.IsIconic(handle)),
                                "rect": [rect.left,rect.top,rect.right,rect.bottom],
                                "client_size": [client.right-client.left,client.bottom-client.top]})
        return True
    if not user.EnumWindows(visit,0):
        raise c.WinError(c.get_last_error())
    return windows


def filter_modules(pids):
    # EnumProcessModulesEx explicitly includes WOW64 modules; the Windows
    # PowerShell/.NET Modules property can return an empty set for x86 players.
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.K32EnumProcessModulesEx.argtypes = [w.HANDLE,c.POINTER(w.HMODULE),w.DWORD,c.POINTER(w.DWORD),w.DWORD]
    kernel.K32GetModuleFileNameExW.argtypes = [w.HANDLE,w.HMODULE,w.LPWSTR,w.DWORD]
    records = []
    for pid in pids:
        handle = kernel.OpenProcess(0x400 | 0x10,False,pid)
        if not handle:
            raise c.WinError(c.get_last_error())
        try:
            modules = (w.HMODULE * 2048)()
            needed = w.DWORD()
            if not kernel.K32EnumProcessModulesEx(handle,modules,c.sizeof(modules),c.byref(needed),3):
                raise c.WinError(c.get_last_error())
            if needed.value > c.sizeof(modules):
                raise RuntimeError("Module snapshot exceeded its bound")
            for index in range(needed.value // c.sizeof(w.HMODULE)):
                name = c.create_unicode_buffer(32768)
                if not kernel.K32GetModuleFileNameExW(handle,modules[index],name,len(name)):
                    raise c.WinError(c.get_last_error())
                path = Path(name.value)
                if re.search(r"xy.*sub|vsfilter|lav|mpc.*render|madvr",str(path),re.I):
                    records.append({"ProcessId":pid,"ModuleName":path.name,"FileName":str(path)})
        finally:
            kernel.CloseHandle(handle)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--player", type=Path, default=Path("Z:/zpAI/zplayer.exe"))
    parser.add_argument("--media", type=Path, default=Path("F:/Git/xySubFilter/Subtitles_CPU_killer_on_end_credits.mkv"))
    parser.add_argument("--port", type=int, default=33001)
    parser.add_argument("--engine", choices=("internal32", "external32", "external64"), default="internal32")
    parser.add_argument("--expected-filter", type=Path)
    parser.add_argument("--profile-session", type=int)
    parser.add_argument("--attach-pid", type=int, help="Use an already running Debug instance on this port")
    parser.add_argument("--build-record", type=Path, required=True,
                        help="Successful Debug-summary.json from Zoom Player's Build.ps1")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--startup-poll-interval", type=float, default=1.0,
                        help="Diagnostic polling interval for the first two seconds (default: 1)")
    args = parser.parse_args()
    if not 0 < args.startup_poll_interval <= 1:
        parser.error("startup poll interval must be in (0, 1]")
    bits = 64 if args.engine == "external64" else 32
    reg_view = winreg.KEY_WOW64_64KEY if bits == 64 else winreg.KEY_WOW64_32KEY
    args.output.mkdir(parents=True, exist_ok=False)
    metadata = {"player": str(args.player), "media": str(args.media), "seek_seconds": 1340,
                "player_sha256": hashlib.sha256(args.player.read_bytes()).hexdigest(),
                "logical_cpus": os.cpu_count(), "default_subtitle": True,
                "startup_poll_interval_s": args.startup_poll_interval, "engine": args.engine, "filter_bitness": bits}
    build = json.loads(args.build_record.read_text(encoding="utf-8-sig"))
    if not build.get("Success") or build.get("Configuration") != "Debug":
        raise ValueError("A successful Debug build is required")
    recorded = next((item for item in build["Outputs"]
                     if Path(item["Path"]).resolve() == args.player.resolve()), None)
    if not recorded or recorded["SHA256"].lower() != metadata["player_sha256"]:
        raise ValueError("Player does not match the Debug build record")
    metadata["debug_build_record"] = build
    metadata["cpu"] = powershell_json("Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors")
    metadata["gpu"] = powershell_json("Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,CurrentRefreshRate")
    metadata["os"] = powershell_json("Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber")
    metadata["settings_sha256"] = hashlib.sha256((args.player.parent / "zplayer.local").read_bytes()).hexdigest()
    metadata["registered_filters"] = []
    for name, guid in (("XySubFilter", "{2DFCB782-EC20-4A7C-B530-4577ADB33F21}"),
                       ("madVR", "{E1A8B82A-32CE-4B0D-BE0D-AA68C772E423}"),
                       ("LAV Video", "{EE30215D-164F-4A92-A4EB-9D4C13390F9F}")):
        try:
            with winreg.OpenKey(winreg.HKEY_CLASSES_ROOT, "CLSID\\" + guid + "\\InprocServer32",
                                0, winreg.KEY_READ | reg_view) as key:
                path = Path(winreg.QueryValueEx(key, None)[0])
            metadata["registered_filters"].append({"name": name, "clsid": guid, "path": str(path),
                                                   "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        except OSError as error:
            metadata["registered_filters"].append({"name": name, "error": str(error)})
    log_offsets = {path: path.stat().st_size for path in
                   (Path("C:/Log/.directshow.txt"), Path("C:/Log/.SmartPlay.txt"),
                    Path("C:/Log/.Main.txt"), Path("C:/Log/.DirectShow_Events.txt"), Path("C:/Log/TCP.txt"))
                   if path.exists()}
    player = (SimpleNamespace(pid=args.attach_pid, poll=lambda: None) if args.attach_pid else
              subprocess.Popen([str(args.player), "/MultInst", f"/TCP:{args.port}"], cwd=args.player.parent))
    metadata["player_pid"] = player.pid
    tcp = cpu = None
    profiling = False
    collector = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\Team Tools\DiagnosticsHub\Collector")
    try:
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            try:
                tcp = TCP(args.port)
                break
            except OSError:
                if player.poll() is not None:
                    raise RuntimeError(f"Player exited with {player.returncode}")
                time.sleep(.2)
        if tcp is None:
            raise TimeoutError("Player TCP startup timed out")
        # Retain the graph/counters at EOF rather than replaying or advancing a playlist.
        tcp.query("5110", data="exPlayComplete,0")
        tcp.query("1920")
        tcp.send("1935 " + str(args.media))
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            # Commands sent while the graph is loading can be ignored by the player.
            try:
                if (Path(tcp.query("1800", timeout=2)).resolve() == args.media.resolve()
                        and tcp.query("1110", timeout=2)
                        and parse_stats("1150 " + tcp.query("1150", timeout=2))["renderer"]):
                    break
            except (TimeoutError, socket.timeout):
                pass
            time.sleep(.2)
        else:
            raise TimeoutError("Media did not start")
        deadline = time.monotonic() + 30
        while True:
            state = tcp.query("1000")
            if state == "2":
                break
            if time.monotonic() > deadline:
                raise TimeoutError("Player did not pause before seeking")
            # A ready metadata snapshot can arrive before asynchronous Run.
            # Retry once playing instead of sending Pause only during loading.
            if state == "3":
                tcp.send("5100 fnPause")
            time.sleep(.5)
        # Zoom's saved state may hide all subtitles; select the sample's flagged default.
        tcp.send("1703 0")
        if tcp.query("1700") != "0":
            raise RuntimeError("The default subtitle track did not become active")
        tcp.send("5000 1340")
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            initial = tcp.stats()
            if initial["position_ms"] is not None and abs(initial["position_ms"] - 1340000) < 100:
                break
            time.sleep(.1)
        else:
            raise TimeoutError("Seek to 22:20 did not complete")
        metadata["initial_stats"] = initial
        metadata["subtitle_index"] = tcp.query("1700")
        metadata["subtitle_name"] = next((event["message"][5:] for event in reversed(tcp.messages)
                                          if event["message"].startswith("1705 ")), None)
        metadata["duration_ms"] = int(tcp.query("1110"))
        metadata["processes"] = powershell_json("Get-CimInstance Win32_Process | Where-Object { $_.Name -in @('zplayer.exe','zpmediaengine.exe','zpmediaengine64.exe') } | Select-Object ProcessId,ParentProcessId,ExecutablePath")
        pids = [player.pid]
        processes = metadata["processes"] or []
        if isinstance(processes, dict):
            processes = [processes]
        pids.extend(p["ProcessId"] for p in processes if p["ParentProcessId"] == player.pid)
        metadata["filter_performance_registry"] = {}
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r"Software\Gabest\XySubFilter\Performance",0,winreg.KEY_READ|reg_view) as key:
                for name in ("bitmap_mru_cache_item_num","max_cache_size_mb"):
                    try:
                        metadata["filter_performance_registry"][name] = winreg.QueryValueEx(key,name)[0]
                    except FileNotFoundError:
                        pass
        except FileNotFoundError:
            pass
        metadata["player_windows"] = window_snapshot(pids)
        pid_list = ",".join(str(pid) for pid in pids)
        metadata["filter_modules"] = filter_modules(pids)
        modules = metadata["filter_modules"] or []
        if isinstance(modules, dict):
            modules = [modules]
        engine_pid = player.pid
        if args.engine != "internal32":
            helper_name = "zpmediaengine64.exe" if bits == 64 else "zpmediaengine.exe"
            helper = next((p for p in processes if p["ParentProcessId"] == player.pid and Path(p["ExecutablePath"]).name.lower() == helper_name), None)
            if not helper:
                raise RuntimeError("Requested external engine was not loaded; refusing a fallback benchmark")
            engine_pid = helper["ProcessId"]
            helper_path = Path(helper["ExecutablePath"])
            helper_build = next((item for item in build["Outputs"] if Path(item["Path"]).resolve() == helper_path.resolve()), None)
            helper_hash = hashlib.sha256(helper_path.read_bytes()).hexdigest()
            if not helper_build or helper_build["SHA256"].lower() != helper_hash or pe_machine(helper_path) != (0x8664 if bits == 64 else 0x14c):
                raise RuntimeError("External helper does not match the expected Debug build/architecture")
            metadata["engine_executable"] = {"path": str(helper_path), "sha256": helper_hash}
        metadata["engine_pid"] = engine_pid
        if args.expected_filter:
            matching = [m for m in modules if m["ProcessId"] == engine_pid and Path(m["FileName"]).resolve() == args.expected_filter.resolve()]
            if not matching or pe_machine(args.expected_filter) != (0x8664 if bits == 64 else 0x14c):
                raise RuntimeError("Expected subtitle DLL was not loaded in the requested engine")
            metadata["verified_filter"] = {"path": str(args.expected_filter.resolve()), "sha256": hashlib.sha256(args.expected_filter.read_bytes()).hexdigest(), "pid": engine_pid}

        (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        metadata["active_filter_dependencies"] = [
            {"name":m["ModuleName"],"path":m["FileName"],"sha256":hashlib.sha256(Path(m["FileName"]).read_bytes()).hexdigest()}
            for m in modules if m["ProcessId"] == engine_pid and re.fullmatch(r"LAVVideo\.ax|MpcVideoRenderer(?:64)?\.ax|madVR(?:64)?\.ax",m["ModuleName"],re.I)]
        if args.profile_session is not None:
            subprocess.run([str(collector / "VSDiagnostics.exe"),"start",str(args.profile_session),
                            f"/attach:{engine_pid}","/loadConfig:AgentConfigs\\CpuUsageLow.json"],cwd=collector,check=True)
            profiling = True
            metadata["profile_pid"] = engine_pid
        cpu = CpuCapture(player.pid, args.output / "cpu.csv")
        cpu.thread.start()
        if not cpu.ready.wait(5) or cpu.error:
            raise RuntimeError("CPU sampler failed: " + str(cpu.error))
        metadata["seek_stats"] = initial
        # Metadata collection can outlast an asynchronous helper's seek snapshot.
        # Read the actual paused counters immediately before the measured Run.
        initial = tcp.stats()
        if tcp.query("1000") != "2" or initial["position_ms"] is None or abs(initial["position_ms"] - 1340000) >= 100:
            raise RuntimeError("Playback did not stay paused at the benchmark start")
        metadata["initial_stats"] = initial
        print("SETUP " + json.dumps({k: metadata[k] for k in ("player_pid", "initial_stats", "subtitle_index", "duration_ms")}), flush=True)
        columns = ["utc", "elapsed_s", "monotonic_s", "position_ms", "renderer", "frames_drawn",
                   "frames_dropped", "average_fps", "delta_frames_drawn", "delta_frames_dropped",
                   "counter_reset", "reply_ms"]
        start = time.monotonic()
        previous = None
        with (args.output / "renderer.csv").open("x", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=columns)
            writer.writeheader()
            def save(stats, requested):
                nonlocal previous
                now = time.monotonic()
                reset, deltas = counter_deltas(previous, stats)
                writer.writerow({**stats, **deltas, "utc": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
                                 "elapsed_s": f"{now - start:.6f}", "monotonic_s": f"{now:.6f}",
                                 "counter_reset": int(reset), "reply_ms": f"{(now - requested) * 1000:.3f}"})
                handle.flush()
                previous = stats
            save(initial, start)
            metadata["capture_start_monotonic_s"] = start
            tcp.send("5100 fnPlay")
            next_poll = start
            completion = "timeout"
            last_progress_bucket = 0
            while time.monotonic() - start < 220:
                next_poll += args.startup_poll_interval if next_poll - start < 2 else 1
                time.sleep(max(0, next_poll - time.monotonic()))
                requested = time.monotonic()
                stats = tcp.stats()
                save(stats, requested)
                if cpu.error:
                    raise RuntimeError("CPU sampler failed: " + cpu.error)
                position = stats["position_ms"]
                progress_bucket = int((time.monotonic() - start) // 20)
                if progress_bucket > last_progress_bucket:
                    print("PROGRESS " + json.dumps(stats), flush=True)
                    last_progress_bucket = progress_bucket
                eof = any(event["message"].startswith("1855") for event in tcp.messages if event["elapsed_s"] >= start)
                state = tcp.query("1000")
                if eof or (state != "3" and time.monotonic() - start > 5):
                    metadata["final_stats"] = tcp.stats()
                    save(metadata["final_stats"], time.monotonic())
                    completion = "end_of_file" if eof else "playback_stopped"
                    break
            metadata["completion"] = completion
            metadata["capture_end_monotonic_s"] = time.monotonic()
            metadata["capture_wall_seconds"] = metadata["capture_end_monotonic_s"] - start
            print("COMPLETE " + json.dumps({"reason": completion, "seconds": metadata["capture_wall_seconds"], "final": previous}), flush=True)
            if completion == "timeout":
                raise TimeoutError("Playback did not reach the end within 220 seconds")
    except Exception as error:
        metadata["error"] = repr(error)
        metadata["error_utc"] = datetime.now(timezone.utc).isoformat()
        raise
    finally:
        if cpu:
            cpu.stop.set()
            cpu.thread.join(timeout=5)
            metadata["cpu_error"] = cpu.error
        if profiling:
            stopped = subprocess.run([str(collector / "VSDiagnostics.exe"),"stop",str(args.profile_session),
                                      "/output:" + str(args.output / "cpu.diagsession")],cwd=collector)
            metadata["profile_stop_code"] = stopped.returncode
        if tcp:
            (args.output / "tcp.json").write_text(json.dumps(tcp.messages, indent=2), encoding="utf-8")
            tcp.socket.close()
        for path, offset in log_offsets.items():
            if path.exists():
                data = path.read_bytes()
                (args.output / ("zoom-" + path.name.lstrip("."))).write_bytes(data[offset:] if len(data) >= offset else data)
        (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        # Leave the benchmark player and its final renderer counters available for inspection.


if __name__ == "__main__":
    main()
