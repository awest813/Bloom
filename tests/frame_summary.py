import importlib.util
import pathlib
import unittest

path = pathlib.Path(__file__).resolve().parents[1] / 'scripts/summarize-frames.py'
spec = importlib.util.spec_from_file_location('frame_summary', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FrameSummaryTests(unittest.TestCase):
    def rows(self):
        return [dict(sequence=2, duration_ms=1000, presentations=20, vblanks=30,
                     skip_decisions=1, total_decisions=4),
                dict(sequence=4, duration_ms=2000, presentations=30, vblanks=60,
                     skip_decisions=3, total_decisions=12)]

    def test_weighted_frames_and_skips_are_distinct(self):
        result = module.summarize(self.rows())
        self.assertAlmostEqual(result['presentation_fps'], 50 / 3)
        self.assertEqual(result['guest_vblanks_per_second'], 30)
        self.assertEqual(result['skip_decision_percent'], 25)
        self.assertEqual(result['intervals_at_least_19_fps'], 1)

    def test_reject_missing_intervals_and_counter_reset(self):
        rows = self.rows(); rows[1]['sequence'] = 6
        with self.assertRaises(ValueError): module.summarize(rows)
        rows = self.rows(); rows[1]['total_decisions'] = 0
        with self.assertRaises(ValueError): module.summarize(rows)


if __name__ == '__main__':
    unittest.main()
