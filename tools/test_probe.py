import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "probe"))
from calibrate import LANE_GAP, LANE_Y0, air_report, entry_report, ledge_report, parse_log, slide_report, step_report  # noqa: E402

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

    def test_slide_report(self):
        track = [(0.1, 600, LANE_Y0, 400, 2, 0, 0, -50)]  # still dropping: not sliding yet
        track += [(0.2 + 0.1 * k, 590 - k, LANE_Y0, 390 - k, 2, -30 * (k + 1), 0, -40 * (k + 1)) for k in range(6)]
        track[1] = track[1][:5] + (-1000, 0, 0)  # a landing spike among the first two sliding samples: left out
        track.append((1.0, 300, LANE_Y0, 100, 2, -900, 0, -900))  # below the lower third: ignored
        self.assertAlmostEqual(slide_report({"D": track})[0], 500.0)  # speed grows 50 per 0.1 s
    def test_entry_report(self):
        c, s45 = 0.6946584, 0.7193398  # cos, sin 46 degrees (lane 0)
        track = [(0.3, 0, LANE_Y0, 60, 2, 0, 0, -100),  # spawn drop: before walking, not the switch
                 (0.6, 10, LANE_Y0, 44, 1, 100, 0, 0), (0.7, 20, LANE_Y0, 44, 1, 100, 0, 0),
                 (0.8, 25, LANE_Y0, 50, 2, 100 * c, 0, 100 * s45), (0.9, 28, LANE_Y0, 56, 2, 0, 0, 0)]
        along, top = entry_report({"P": track})[0]
        self.assertAlmostEqual(along[0], 100.0, places=3)
        self.assertEqual((len(along), top), (2, 60))

    def test_ledge_report(self):
        y = LANE_Y0 + LANE_GAP  # lane 1
        track = [(0.3, 0, y, 60, 2, 0, 0, -50), (0.5, 0, y, 44, 1, 0, 0, 0),  # spawn drop, then rest
                 (1.0, 238, y, 85, 2, 44, 0, 272), (1.5, 260, y, 124, 2, 70, 0, 10),  # jump against the ledge
                 (1.8, 300, y, 108, 1, 400, 0, 0), (2.5, 1100, y, 44, 1, 400, 0, 0)]  # on top, then off its end
        self.assertEqual(ledge_report({"P": track}), {1: (44, True, 124, 108)})
        stuck = [(0.5, 0, y, 44, 1, 0, 0, 0), (1.0, 238, y, 44, 1, 0, 0, 0)]
        self.assertEqual(ledge_report({"P": stuck}), {1: (44, False, 44, None)})


if __name__ == "__main__":
    unittest.main()
