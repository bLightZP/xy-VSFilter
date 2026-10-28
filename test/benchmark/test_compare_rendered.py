"""Coverage checks for full and focused pixel comparisons."""
import csv
import tempfile
import unittest
from pathlib import Path
from compare_rendered import load


class PixelCoverageTests(unittest.TestCase):
    def write(self, folder, frames, hashed=True):
        path = Path(folder) / 'pixels.csv'
        with path.open('w', newline='', encoding='utf-8') as stream:
            writer = csv.writer(stream)
            writer.writerow(['frame', 'time_100ns', 'render_ms', 'bitmaps', 'sha256'])
            for frame in frames:
                time = (2*frame+1)*1001*10000000//48000
                writer.writerow([frame, time, 1, 1, '0'*64 if hashed else ''])
        return path

    def test_focused_range_requires_every_frame_in_order(self):
        with tempfile.TemporaryDirectory() as folder:
            rows, stats = load(self.write(folder, [32800,32801,32802]),32800,32802)
            self.assertEqual(len(rows),3)
            self.assertEqual(stats['nonempty_frames'],3)
            for invalid in ([32800,32802], [32800,32801,32801], [32801,32800,32802]):
                with self.assertRaises(ValueError):
                    load(self.write(folder,invalid),32800,32802)

    def test_timing_only_output_is_not_pixel_validation(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):
                load(self.write(folder,[32800],hashed=False),32800,32800)

    def test_legacy_extra_eof_frame_remains_accepted(self):
        with tempfile.TemporaryDirectory() as folder:
            rows, _ = load(self.write(folder,range(32128,34557)))
            self.assertEqual(len(rows),2428)
            self.assertEqual(int(rows[-1]['frame']),34555)


if __name__ == '__main__':
    unittest.main()
