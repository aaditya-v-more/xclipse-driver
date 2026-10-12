"""Regression checks for failures that must never become benchmark scores.

Run without a phone: python -m unittest discover -s pipeline -p test_game.py
"""

import argparse
import contextlib
import copy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import game


class FailureDetectionTests(unittest.TestCase):
    def test_recorded_wine_process_names(self):
        for name in (r"A:\bin\Heaven.exe", "Heaven.exe", "heaven.exe"):
            self.assertTrue(game.is_game_process(name, "Heaven.exe"))
        self.assertTrue(game.is_game_process("GravityMark.exe", "GravityMark.exe"))
        self.assertTrue(game.is_game_process("LongBenchmarkNa", "LongBenchmarkName.exe"))
        for name in ("", "Heaven", "notHeaven.exe", "explorer.exe"):
            self.assertFalse(game.is_game_process(name, "Heaven.exe"))

    def test_crash_logs_ignore_other_apps(self):
        own = "10-12 12:00:00.000 100 101 F libc: Fatal signal 11"
        other = "10-12 12:00:00.000 200 201 F libc: Fatal signal 11"
        anr = "10-12 12:00:00.000 1 2 E ActivityManager: ANR in app.gamenative"
        with patch.object(game, "logcat_since", return_value=[own, other, anr]):
            self.assertEqual(game.runtime_crash("", {"100": game.APP}), [own, anr])

    def simulate_run(self, failure):
        """Replay lifecycle failures while the frame counter claims a valid 70-second run."""
        clock = [0.0]

        def sample(_user, now):
            now["100"] = game.APP
            now["101"] = r"A:\bin\Heaven.exe"
            if clock[0] >= 20:
                if failure == "app-exited":
                    now.clear()
                elif failure == "game-exited":
                    now.pop("101")
            return {"available_mb": 1024}

        replacements = {
            "shell": lambda *a, **k: "",
            "gpu_temp": lambda: 40,
            "free_memory": lambda user: 1024,
            "gpu_faults": lambda: {"page_faults": int(failure == "gpu-fault" and clock[0] >= 20),
                                   "resets": int(failure == "gpu-reset" and clock[0] >= 20)},
            "logcat_start": lambda path: None,
            "logcat_stop": lambda: None,
            "phone_state": lambda: "device",
            "gpu_sample": lambda: None,
            "memory_sample": sample,
            "battery_sample": lambda: {},
            "runtime_crash": lambda *a: ["Fatal signal 11"] if failure == "crashed" else [],
            "message_box": lambda: [800, 350, 1250, 600] if failure == "error-box" else None,
            "game_layers": lambda: ["game surface"],
            "present_times": lambda layer: ([1, 2], False, 8333333),
            "frame_stats": lambda *a: {"steady_seconds": 70, "fps_avg": 60},
            "screenshot": lambda path: True,
            "drivers_seen": lambda *a: [],
            "logcat_since": lambda *a: [],
        }
        with contextlib.ExitStack() as stack:
            for name, fn in replacements.items():
                stack.enter_context(patch.object(game, name, fn))
            stack.enter_context(patch.object(game.time, "monotonic", lambda: clock[0]))
            stack.enter_context(patch.object(game.time, "sleep", lambda _: clock.__setitem__(0, clock[0] + 10)))
            return game.run_once("heaven", "stock", Path("unused"), "run1", "user", seconds=90)

    def test_failures_override_sustained_frames(self):
        for failure in ("error-box", "app-exited", "game-exited", "crashed"):
            with self.subTest(failure=failure):
                result = self.simulate_run(failure)
                self.assertEqual(result["status"], failure)
                self.assertIn("screenshot", result)
                result["run"] = 1
                report = game.summarize({"started": "test"}, [result])
                self.assertIn("no valid runs", report)

    def test_gpu_faults_and_resets_stop_before_time_limit_or_ui_capture(self):
        for failure, counter in (("gpu-fault", "page_faults"), ("gpu-reset", "resets")):
            with self.subTest(failure=failure):
                result = self.simulate_run(failure)
                self.assertEqual(result["status"], "gpu-fault")
                self.assertEqual(result[f"gpu_{counter}"], 1)
                self.assertEqual(result["gpu_fault_detected_s"], 20)
                self.assertNotIn("screenshot", result)
                self.assertNotIn("message_box", result)

    def test_repeated_failures_skip_only_affected_container_and_checkpoint(self):
        args = argparse.Namespace(runs=3, max_failures=2, shots="", seconds=90)
        calls, checkpoints = [], []

        def run(bench, driver, *_):
            calls.append(driver)
            return {"bench": bench, "driver": driver,
                    "status": "error-box" if driver == "stock" else "ran",
                    "surfaceflinger": {"fps_avg": 45}, "gpu_page_faults": 0}

        with patch.object(game, "run_once", run), patch.object(game, "cool_down"), \
                patch.object(game, "write_report", lambda p, m, r: checkpoints.append(copy.deepcopy(r))):
            results = game.run_all(args, ["heaven"], ["stock", "radv"], Path("unused"), "user", 40,
                                   {"started": "test"})
        self.assertEqual(calls.count("stock"), 2)
        self.assertEqual(calls.count("radv"), 3)
        self.assertEqual(results[-1]["status"], "ran")
        self.assertEqual(sum(r["status"] == "skipped" for r in results), 1)
        self.assertEqual(len(checkpoints[0]), 1)
        self.assertEqual(checkpoints[-1], results)

    def test_checkpoint_is_readable_after_each_write(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            game.write_report(path, {"started": "test"}, [])
            self.assertTrue((path / "results.json").exists())
            self.assertTrue((path / "summary.md").exists())
            self.assertEqual(list(path.glob("*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
