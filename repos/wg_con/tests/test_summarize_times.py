"""Selected-attempt timing export from finalized WebGraph manifests."""

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
import summarize_times


def bundle(root, graph, order, encode, index):
    directory = root / graph
    directory.mkdir(parents=True)
    sizes = {"graph": 50, "properties": 11, "ef": 8}
    if order == "bfs-llp":
        sizes["perm"] = 16
    for extension, size in sizes.items():
        (directory / (graph + "." + extension)).write_bytes(bytes(size))
    selected_id = order.replace("-", "_") + "_w128"
    manifest = {
        "search_version": 3,
        "selected": {
            "id": selected_id,
            "order": order,
            "runtime_bytes": sum(sizes.values()),
            "files": {extension: {"name": graph + "." + extension,
                                  "bytes": size}
                      for extension, size in sizes.items()},
        },
        "attempts": [
            {"id": "earlier", "status": "success", "order": order,
             "runtime_bytes": sum(sizes.values()) + 10,
             "file_bytes": sizes, "encode_seconds": 100.0,
             "index_seconds": 1.0},
            {"id": selected_id, "status": "success", "order": order,
             "runtime_bytes": sum(sizes.values()),
             "file_bytes": sizes, "encode_seconds": encode,
             "index_seconds": index},
        ],
    }
    (directory / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    return directory, manifest


class SummarizeTimesTests(unittest.TestCase):
    def test_selected_attempt_and_missing_graph(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "format_wg"
            bundle(output, "original", "original", 1.125, 0.25)
            bundle(output, "reordered", "bfs-llp", 5.4, 0.375)
            old, manifest = bundle(output, "old", "original", 1.0, 0.2)
            manifest["search_version"] = 2
            (old / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            graphs = root / "graphs.txt"
            graphs.write_text("# evaluation graphs\nreordered\noriginal\nold\nmissing\n",
                              encoding="utf-8")

            stdout = io.StringIO()
            with mock.patch.object(sys, "argv", [
                    "summarize_times.py", "--output-dir", str(output),
                    "--graphs", str(graphs)]), contextlib.redirect_stdout(stdout):
                summarize_times.main()
            rows = list(csv.DictReader(io.StringIO(stdout.getvalue())))

            self.assertEqual([row["graph"] for row in rows],
                             ["reordered", "original", "old", "missing"])
            self.assertEqual(rows[0]["selected_id"], "bfs_llp_w128")
            self.assertEqual(rows[0]["bundle_bytes"], "85")
            self.assertEqual(rows[0]["encode_seconds"], "5.4")
            self.assertEqual(rows[0]["index_seconds"], "0.375")
            self.assertEqual(rows[0]["selected_attempt_seconds"], "5.775")
            self.assertEqual(rows[1]["selected_attempt_seconds"], "1.375")
            self.assertEqual(rows[2]["status"], "wrong_search_version")
            self.assertEqual(rows[2]["encode_seconds"], "")
            self.assertEqual(rows[3]["status"], "missing")
            self.assertEqual(rows[3]["encode_seconds"], "")

    def test_mismatched_final_bundle_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            directory, _ = bundle(output, "graph", "original", 1.0, 0.2)
            (directory / "graph.ef").write_bytes(bytes(9))
            with self.assertRaisesRegex(ValueError, "selected file differs"):
                summarize_times.selected_attempt(directory, "graph")

    def test_missing_selected_success_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            directory, manifest = bundle(output, "graph", "original", 1.0, 0.2)
            manifest["attempts"][1]["status"] = "timeout"
            (directory / "manifest.json").write_text(json.dumps(manifest),
                                                      encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "one successful selected attempt"):
                summarize_times.selected_attempt(directory, "graph")


if __name__ == "__main__":
    unittest.main()
