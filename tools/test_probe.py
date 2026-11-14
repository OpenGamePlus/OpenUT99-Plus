import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "probe"))
from calibrate import LANE_GAP, LANE_Y0, air_report, parse_log, step_report  # noqa: E402

LOG = """Log: Bringing Level OU-Steps.MyLevel up for play (20)...
ScriptLog: PROBE ProbePawn0 0.25 0.00 -1050.00 45.00 2 0.00 0.00 -200.00
ScriptLog: PROBE ProbePawn0 0.50 0.00 -1050.00 39.00 1 0.00 0.00 0.00
ScriptLog: PROBE spawnfail 1.0 2.0 3.0
ScriptLog: PROBE ProbePawn1 0.50 0.00 -750.00 39.00 1 0.00 0.00 0.00
ScriptLog: PROBE ProbePawn0 2.00 600.00 -1050.00 55.00 1 400.00 0.00 0.00
ScriptLog: PROBE ProbePawn1 2.00 255.00 -750.00 39.00 1 0.00 0.00 0.00
"""


class ProbeLogTest(unittest.TestCase):
    def test_parse(self):
        tracks = parse_log(LOG)
        self.assertEqual(sorted(tracks), ["ProbePawn0", "ProbePawn1"])  # spawnfail is not a pawn
        self.assertEqual(tracks["ProbePawn0"][1], (0.5, 0.0, -1050.0, 39.0, 1, 0.0, 0.0, 0.0))
        self.assertIsInstance(tracks["ProbePawn0"][0][4], int)

    def test_step_report(self):
        report = step_report(parse_log(LOG))
        self.assertEqual(report, {0: (39.0, 55.0), 1: (39.0, 39.0)})  # lane 0 climbed 16, lane 1 stayed
        self.assertEqual(step_report({"P": [(0.6, 0, LANE_Y0 + LANE_GAP, 0, 1, 0, 0, 0)]}), {})  # spawned late

    def test_air_report(self):
        y = LANE_Y0 + LANE_GAP  # lane 1
        track = [(0.05, 0, y, 1800, 2, 0, 0, 0), (0.2, 0, y, 1790, 2, 30, 40, -100),  # speed 50
                 (1.2, 10, y, 900, 2, 90, 120, -900), (1.3, 20, y, 800, 1, 0, 0, 0)]  # speed 150; then landed
        self.assertEqual(air_report({"F": track}), {1: (100.0, 150.0)})


if __name__ == "__main__":
    unittest.main()
