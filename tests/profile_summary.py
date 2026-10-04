"""Checks for weighted FPS and invalid capture rejection."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "profile_summary", Path(__file__).resolve().parents[1] / "scripts/summarize-profile.py")
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


def interval(sequence, seconds, frames):
    row = {name + "_us": 0 for name in profile.PARTS}
    row.update(sequence=sequence, duration_us=seconds * 1000000,
               presentations=frames, vblanks=frames * 2)
    row["ps1_us"] = row["duration_us"]
    return row


class SummaryTests(unittest.TestCase):
    def test_io_partition_and_layout_mismatch(self):
        row = interval(2, 5, 50)
        row.update(ps1_us=4000000, io_us=1000000)
        result = profile.summarize([row])
        self.assertEqual(result["exclusive_percent"]["io"], 20)
        self.assertEqual(result["exclusive_percent"]["ps1"], 80)
        with self.assertRaises(ValueError):
            profile.summarize([row, interval(4, 5, 50)])

    def test_weighted_rate(self):
        result = profile.summarize([interval(2, 5, 50), interval(4, 10, 150)])
        self.assertAlmostEqual(result["presentation_fps"], 200 / 15)
        self.assertAlmostEqual(result["vblanks_per_second"], 400 / 15)
        self.assertEqual(result["exclusive_percent"]["ps1"], 100)

    def test_reject_invalid_intervals(self):
        bad_partition = interval(2, 5, 50)
        bad_partition["draw_us"] = 1
        for rows in ([], [interval(3, 5, 50)], [interval(2, 0, 0)],
                     [interval(2, 5, -1)], [bad_partition],
                     [interval(2, 5, 50), interval(2, 5, 50)]):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                profile.summarize(rows)


if __name__ == "__main__":
    unittest.main()
