"""Successful-trial totals must not count resume history twice."""

import contextlib
import csv
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import summarize_search_times


class SearchTimesTests(unittest.TestCase):
    def test_successful_attempts_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "wg" / "graph"
            directory.mkdir(parents=True)
            first = {"id": "original_w16", "status": "success",
                     "encode_seconds": 1.25, "index_seconds": 0.125}
            selected = {"id": "bfs_llp_w128", "status": "success",
                        "encode_seconds": 4.5, "index_seconds": 0.25}
            manifest = {
                "search_version": 3,
                "selected": {"id": "bfs_llp_w128"},
                "attempts": [first, {"id": "bfs_llp_w64", "status": "timeout"}, selected],
                "attempt_history": [first, {"id": "bfs_llp_w64", "status": "timeout"}],
            }
            (directory / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            graphs = root / "graphs.txt"
            graphs.write_text("graph\nmissing\n", encoding="utf-8")

            stdout = io.StringIO()
            with mock.patch.object(sys, "argv", [
                    "summarize_search_times.py", "--output-dir", str(root / "wg"),
                    "--graphs", str(graphs)]), contextlib.redirect_stdout(stdout):
                summarize_search_times.main()
            rows = list(csv.DictReader(io.StringIO(stdout.getvalue())))
            self.assertEqual(rows[0]["successful_trials"], "2")
            self.assertEqual(rows[0]["encode_seconds"], "5.75")
            self.assertEqual(rows[0]["index_seconds"], "0.375")
            self.assertEqual(rows[0]["successful_attempt_seconds"], "6.125")
            self.assertEqual(rows[1]["status"], "missing")

    def test_selected_attempt_must_be_successful(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            manifest = {
                "search_version": 3,
                "selected": {"id": "winner"},
                "attempts": [{"id": "other", "status": "success",
                              "encode_seconds": 1.0, "index_seconds": 0.1}],
            }
            (directory / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "selected candidate"):
                summarize_search_times.successful_time(directory)


if __name__ == "__main__":
    unittest.main()
