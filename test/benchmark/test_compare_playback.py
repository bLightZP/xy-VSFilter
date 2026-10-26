"""Metric gates for independently continuous renderer drop counters."""
import csv
import json
from pathlib import Path
import tempfile
import unittest

from compare_playback import summarize


class PlaybackMetrics(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.rows = [
            dict(elapsed_s="0",position_ms="1340000",renderer="MPC Video Renderer",frames_drawn="51",frames_dropped="0",counter_reset="0",delta_frames_dropped="",reply_ms="1"),
            dict(elapsed_s="1",position_ms="1341000",renderer="MPC Video Renderer",frames_drawn="23",frames_dropped="0",counter_reset="1",delta_frames_dropped="",reply_ms="1"),
            dict(elapsed_s="102",position_ms="1441272",renderer="MPC Video Renderer",frames_drawn="2391",frames_dropped="37",counter_reset="0",delta_frames_dropped="37",reply_ms="1"),
        ]
        metadata = dict(completion="end_of_file",subtitle_index="0",default_subtitle=True,duration_ms=1441272,capture_wall_seconds=102,logical_cpus=8,registered_filters=[dict(name="XySubFilter")],player_sha256="fixture",media="fixture.mkv")
        (self.folder/"metadata.json").write_text(json.dumps(metadata),encoding="utf-8")
        with (self.folder/"cpu.csv").open("w",newline="") as file:
            writer=csv.DictWriter(file,fieldnames=["pid","cpu_seconds","cpu_cores","private_bytes","monotonic_s"])
            writer.writeheader()
            for row in [dict(pid=1,cpu_seconds=10,cpu_cores=0,private_bytes=100,monotonic_s=0),dict(pid=2,cpu_seconds=1,cpu_cores=0,private_bytes=200,monotonic_s=0),dict(pid=1,cpu_seconds=12,cpu_cores=1,private_bytes=110,monotonic_s=102),dict(pid=2,cpu_seconds=4,cpu_cores=1,private_bytes=220,monotonic_s=102)]:
                writer.writerow(row)

    def result(self,rows=None):
        rows = self.rows if rows is None else rows
        with (self.folder/"renderer.csv").open("w",newline="") as file:
            writer=csv.DictWriter(file,fieldnames=rows[0].keys()); writer.writeheader(); writer.writerows(rows)
        return summarize(self.folder)

    def test_drawn_reset_keeps_continuous_drop_count_and_process_cpu(self):
        result=self.result()
        self.assertEqual(result["drops"],37)
        self.assertEqual(result["cpu_seconds"],5)
        self.assertEqual(result["peak_combined_private_bytes"],330)
        self.assertEqual(len(result["drawn_counter_resets"]),1)

    def test_first_interval_drop_is_included_despite_drawn_reset(self):
        self.rows[1]["frames_dropped"]="1"
        self.rows[2]["delta_frames_dropped"]="36"
        self.assertEqual(self.result()["drops"],37)

    def test_reject_drop_reset(self):
        self.rows[0]["frames_dropped"]="2"
        with self.assertRaises(ValueError): self.result()

    def test_reject_late_drawn_reset(self):
        self.rows[1]["elapsed_s"]="3"
        with self.assertRaises(ValueError): self.result()

    def test_reject_renderer_change(self):
        self.rows[1]["renderer"]="different"
        with self.assertRaises(ValueError): self.result()

    def test_reject_seek_during_capture(self):
        self.rows[1]["position_ms"]="1339000"
        with self.assertRaises(ValueError): self.result()

    def test_reject_unavailable_drop_count(self):
        self.rows[1]["frames_dropped"]=""
        with self.assertRaises(ValueError): self.result()

    def test_reject_incorrect_drop_delta(self):
        self.rows[2]["delta_frames_dropped"]="36"
        with self.assertRaises(ValueError): self.result()


if __name__ == "__main__":
    unittest.main()
