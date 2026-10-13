"""The paired report must exclude incomplete windows and unreliable frame samples."""
import copy
import unittest

import game
import game_compare


class GameComparisonTests(unittest.TestCase):
    def test_fixed_mode_cannot_score_different_or_changing_clocks(self):
        def row(role, clock):
            return {"repeat": 1, "candidate": "option", "role": role, "accepted": True,
                    "seconds_to_first_frame": 0, "surfaceflinger": {"fps_avg": 40},
                    "gpu": [[t, 100, clock, 55, 1, 1, 1, 1632000] for t in range(10, 30)]}
        rows = [row("baseline", 400000), row("candidate", 252000)]
        fixed = {"fixed_clocks": True}
        self.assertEqual([r["status"] for r in game_compare.scored_rows(fixed, rows)],
                         ["clock-excluded", "clock-excluded"])
        self.assertTrue(all(r["accepted"] for r in rows))  # raw measurements remain intact
        rows[1] = row("candidate", 400000)
        self.assertTrue(all(r["status"] == "pass" for r in game_compare.scored_rows(fixed, rows)))
        rows[1]["gpu"][0][7] = 1056000
        self.assertTrue(all(r["status"] == "clock-excluded" for r in game_compare.scored_rows(fixed, rows)))
        self.assertTrue(all(r["status"] == "pass" for r in game_compare.scored_rows({"fixed_clocks": False}, rows)))
        rows[1]["gpu"] = []
        self.assertTrue(all(r["status"] == "clock-excluded" for r in game_compare.scored_rows(fixed, rows)))

    def test_fixed_measurement_window_excludes_slow_tail(self):
        # 60 FPS for the ten-second warm-up plus 60 measured seconds; then a 10 FPS tail.
        times = [1_000_000_000 + round(n * 1e9 / 60) for n in range(70 * 60)]
        times += [times[-1] + n * 100_000_000 for n in range(1, 201)]
        clipped = game.frame_stats(times, duration_s=60)
        self.assertAlmostEqual(clipped["fps_avg"], 60, places=3)
        self.assertGreater(clipped["steady_seconds"], 59.9)
        self.assertLess(game.frame_stats(times)["fps_avg"], 50)

    def test_exclusions_override_a_plausible_fps_score(self):
        good = {"status": "ran", "surfaceflinger": {"fps_avg": 60, "steady_seconds": 89.99,
                "sampling_gaps": 0, "covered_share": 1}, "screens": ["one.png", "two.png"],
                "gpu": [[400000]], "gpu_page_faults": 0, "gpu_resets": 0, "gpu_temp_c_before": 45}
        self.assertTrue(game_compare.accepted(good, 90, 45))
        for field, value in (("status", "crashed"), ("screens", []), ("gpu", []),
                             ("gpu_page_faults", 1), ("gpu_resets", 1), ("gpu_temp_c_before", 46),
                             ("gpu_temp_c_before", 0), ("gpu_temp_c_before", float("nan"))):
            self.assertFalse(game_compare.accepted({**good, field: value}, 90, 45))
        for field, value in (("steady_seconds", 70), ("sampling_gaps", 1), ("covered_share", .95)):
            bad = copy.deepcopy(good)
            bad["surfaceflinger"][field] = value
            self.assertFalse(game_compare.accepted(bad, 90, 45))


if __name__ == "__main__":
    unittest.main()
