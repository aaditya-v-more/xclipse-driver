"""Checks that failed attempts and unmatched repetitions cannot produce benchmark gains."""
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import compare
import run


class ComparisonTests(unittest.TestCase):
    def test_pair_order_is_counterbalanced(self):
        variants = [{"name": n} for n in ("base", "one", "two")]
        schedule = list(compare.pairs(variants, 6))
        for candidate in ("one", "two"):
            orders = [tuple(v["name"] for v in order) for _, c, order in schedule if c == candidate]
            self.assertEqual(orders.count(("base", candidate)), 3)
            self.assertEqual(orders.count((candidate, "base")), 3)

    def test_only_complete_successful_pairs_count(self):
        rows = []
        for repeat, base, candidate in ((1, 10, 5), (2, 100, 1), (3, 1, 100)):
            for role, value in (("baseline", base), ("candidate", candidate)):
                rows.append(dict(test="draws", candidate="opt", repeat=repeat, role=role,
                                 status="pass", metric=value))
        rows[3]["status"] = "gpu-hang"
        rows[4]["status"] = "missing-telemetry"
        self.assertEqual(compare.compare_values(rows, "draws", "opt", "metric", False),
                         (1, 10, 5, 100, 100, 100))

    def test_gpu_fault_overrides_test_reported_pass(self):
        with tempfile.TemporaryDirectory() as temp, \
             patch.object(run, "shell", return_value=""), \
             patch.object(run, "adb", return_value=SimpleNamespace(stdout='{"status":"pass"}',
                                                                    stderr="", returncode=0)), \
             patch.object(run, "phone_state", return_value="device"), \
             patch.object(run, "driver_log", return_value=[]), \
             patch.object(run, "gpu_event_counters", side_effect=[{"GPF_S": 3}, {"GPF_S": 4}]):
            result = run.run_test("radv", "draws", Path(temp), set(), [])
            self.assertEqual(result["status"], "gpu-hang")
            self.assertEqual(result["gpu_events"], {"page fault": 1})


if __name__ == "__main__":
    unittest.main()
