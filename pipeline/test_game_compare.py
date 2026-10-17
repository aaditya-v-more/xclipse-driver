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

    def heaven_launch(self, stall_s=3.2):
        """Frame times shaped like Heaven's launch on 2026-10-17: sparse booting frames, a 2 s
        LOADING burst at 45 FPS, a loading stall, then 120 s of rendering at 60 FPS."""
        s = 1_000_000_000
        times = [4 * s + n * s for n in range(10)]                  # booting, 1 FPS
        burst = 14.7 * s
        times += [round(burst + n * s / 45) for n in range(100)]     # LOADING screen
        rendering = times[-1] + round(stall_s * s)
        times += [round(rendering + n * s / 60) for n in range(120 * 60)]
        return times, rendering

    def test_window_starts_after_the_last_loading_stall(self):
        times, rendering = self.heaven_launch()
        self.assertEqual(game.rendering_start(times), rendering)
        stats = game.frame_stats(times, duration_s=90)
        self.assertEqual(stats["rendering_ns"], rendering)
        self.assertAlmostEqual(stats["fps_avg"], 60, places=3)
        self.assertGreaterEqual(stats["steady_seconds"], 89.75)
        # Before the fix the LOADING burst started the clock, so warm-up ended 4.2 s after rendering
        # began, by an amount that differed between runs.
        self.assertGreater(rendering - times[10], 4 * 1_000_000_000)

    def test_rendering_without_a_loading_stall_starts_at_the_first_burst(self):
        s = 1_000_000_000
        times = [n * s for n in range(5)] + [round(5 * s + n * s / 60) for n in range(60 * 60)]
        self.assertEqual(game.rendering_start(times), 4 * s)  # booting frames 1 s apart, then 60 FPS

    def test_a_sampling_hole_is_not_a_loading_stall(self):
        times, rendering = self.heaven_launch()
        cut = next(i for i, t in enumerate(times) if t > rendering + 20 * 1_000_000_000)
        gapped = times[:cut] + times[cut + 300:]  # 5 s of frames lost to a ring overflow
        self.assertNotEqual(game.rendering_start(gapped), rendering)
        hole = [(gapped[cut - 1], gapped[cut])]
        self.assertEqual(game.rendering_start(gapped, hole), rendering)
        self.assertEqual(game.frame_stats(gapped, hole, 90)["sampling_gaps"], 1)

    def test_no_window_until_rendering_lasts_min_steady(self):
        times, rendering = self.heaven_launch()
        early = [t for t in times if t < rendering + 20 * 1_000_000_000]
        self.assertIsNone(game.rendering_start(early))
        self.assertNotIn("seconds", game.frame_stats(early))

    def test_clock_window_follows_rendering_start(self):
        r = {"seconds_to_first_frame": 0, "seconds_to_rendering": 20,
             "gpu": [[t, 100, 252000 if t < 30 else 400000, 55, 1, 1, 1, 1632000] for t in range(0, 140)]}
        self.assertEqual(game_compare.active_clocks(r, 90), {(400000, 1632000): 91})
        del r["seconds_to_rendering"]  # older results: first surface frame
        self.assertEqual(len(game_compare.active_clocks(r)), 2)

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
