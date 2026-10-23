"""Poll Zoom Player's TCP 1150 response into CSV using the Python standard library."""

import argparse
import csv
import socket
import time
from datetime import datetime, timezone
from pathlib import Path


def parse_stats(line):
    if not line.startswith("1150 "):
        raise ValueError("Expected a 1150 response")
    fields = line[5:].split("|")
    if len(fields) != 5:
        raise ValueError("Expected five renderer statistics fields")
    position, renderer, drawn, dropped, fps = fields
    return {
        "position_ms": int(position) if position else None,
        "renderer": renderer or None,
        "frames_drawn": int(drawn) if drawn else None,
        "frames_dropped": int(dropped) if dropped else None,
        "average_fps": float(fps) if fps else None,
    }


def counter_deltas(previous, current):
    """Counters are cumulative; never count a seek/reset as negative drops."""
    reset = False
    if previous is not None:
        reset = previous["renderer"] != current["renderer"]
        for field in ("position_ms", "frames_drawn", "frames_dropped"):
            before, after = previous[field], current[field]
            if before is not None and after is not None and after < before:
                reset = True
    deltas = {}
    for field in ("frames_drawn", "frames_dropped"):
        before = previous[field] if previous else None
        after = current[field]
        deltas["delta_" + field] = (
            after - before
            if not reset and before is not None and after is not None
            else None
        )
    return reset, deltas


class RendererConnection:
    def __init__(self, host, port, timeout):
        self.socket = socket.create_connection((host, port), timeout=timeout)
        self.timeout = timeout
        self.buffer = b""

    def close(self):
        self.socket.close()

    def query(self):
        self.socket.sendall(b"1150\r\n")
        deadline = time.monotonic() + self.timeout
        while True:
            while b"\r\n" in self.buffer:
                line, self.buffer = self.buffer.split(b"\r\n", 1)
                # Initial notifications and other unsolicited messages are allowed.
                if line.startswith(b"1150 "):
                    return parse_stats(line.decode("utf-8"))
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("No 1150 reply before the timeout")
            self.socket.settimeout(remaining)
            data = self.socket.recv(65536)
            if not data:
                raise ConnectionError("Zoom Player closed the TCP connection")
            self.buffer += data
            if len(self.buffer) > 1024 * 1024:
                raise ValueError("Unterminated TCP message exceeds 1 MiB")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=32999)
    parser.add_argument("--seconds", type=float, default=110)
    parser.add_argument("--interval", type=float, default=1)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.seconds < 0 or args.interval <= 0 or args.timeout <= 0:
        parser.error("seconds must be nonnegative; interval and timeout must be positive")

    columns = [
        "utc", "elapsed_s", "position_ms", "renderer", "frames_drawn",
        "frames_dropped", "average_fps", "delta_frames_drawn",
        "delta_frames_dropped", "counter_reset", "reply_ms",
    ]
    connection = RendererConnection(args.host, args.port, args.timeout)
    try:
        # Exclusive creation protects an earlier benchmark result.
        with args.output.open("x", newline="", encoding="utf-8") as output:
            writer = csv.DictWriter(output, fieldnames=columns)
            writer.writeheader()
            previous = None
            start = next_poll = time.monotonic()
            while True:
                time.sleep(max(0, next_poll - time.monotonic()))
                requested = time.monotonic()
                stats = connection.query()
                received = time.monotonic()
                reset, deltas = counter_deltas(previous, stats)
                writer.writerow({
                    **stats,
                    **deltas,
                    "average_fps": (
                        f"{stats['average_fps']:.3f}"
                        if stats["average_fps"] is not None else None
                    ),
                    "utc": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
                    "elapsed_s": f"{received - start:.3f}",
                    "counter_reset": int(reset),
                    "reply_ms": f"{(received - requested) * 1000:.3f}",
                })
                output.flush()
                previous = stats
                next_poll += args.interval
                # Skip missed polling slots instead of issuing a burst of queries.
                while next_poll < received:
                    next_poll += args.interval
                if next_poll > start + args.seconds:
                    break
    finally:
        connection.close()
    print(f"Saved renderer statistics to {args.output}")


if __name__ == "__main__":
    main()
