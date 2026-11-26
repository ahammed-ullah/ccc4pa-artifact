#!/usr/bin/env python3
"""Check construction-time export against final bundle manifests."""

import csv
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "summarize_times.py"


def bundle(root, graph, order, seconds):
    directory = root / graph
    directory.mkdir()
    files = {}
    extensions = ("zk", "zki") + (("perm",) if order == "bfs-llp" else ())
    for extension in extensions:
        contents = (graph + extension).encode("ascii")
        path = directory / (graph + "." + extension)
        path.write_bytes(contents)
        files[extension] = {"name": path.name, "bytes": len(contents)}
    manifest = {
        "graph": graph,
        "order": order,
        "files": files,
        "runtime_bytes": sum(info["bytes"] for info in files.values()),
        "encode_seconds": seconds,
    }
    (directory / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    return directory


class ConstructionTimesTest(unittest.TestCase):
    def test_complete_and_missing_graphs_follow_graph_list_order(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            graphs = root / "graphs.txt"
            graphs.write_text("alpha\n# comment\nmissing\nbeta\n", encoding="utf-8")
            bundle(root, "alpha", "original", 3.25)
            bundle(root, "beta", "bfs-llp", 9.5)
            completed = subprocess.run(
                [sys.executable, str(SCRIPT), "--output-dir", str(root),
                 "--graphs", str(graphs)],
                universal_newlines=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, check=True,
            )
            rows = list(csv.reader(io.StringIO(completed.stdout)))
            self.assertEqual(rows[0],
                             ["graph", "status", "order", "bundle_bytes", "encode_seconds"])
            self.assertEqual(rows[1], ["alpha", "complete", "original", "15", "3.25"])
            self.assertEqual(rows[2], ["missing", "missing", "", "", ""])
            self.assertEqual(rows[3], ["beta", "complete", "bfs-llp", "21", "9.5"])

    def test_changed_bundle_and_invalid_time_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            graphs = root / "graphs.txt"
            graphs.write_text("alpha\n", encoding="utf-8")
            directory = bundle(root, "alpha", "original", 3.25)
            command = [sys.executable, str(SCRIPT), "--output-dir", str(root),
                       "--graphs", str(graphs)]
            (directory / "alpha.zk").write_bytes(b"changed!")
            changed = subprocess.run(command, universal_newlines=True,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(changed.returncode, 0)
            self.assertIn("bundle file size differs", changed.stderr)

            bundle_manifest = directory / "manifest.json"
            data = json.loads(bundle_manifest.read_text(encoding="utf-8"))
            (directory / "alpha.zk").write_bytes(b"alphazk")
            data["encode_seconds"] = -1
            bundle_manifest.write_text(json.dumps(data), encoding="utf-8")
            bad_time = subprocess.run(command, universal_newlines=True,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(bad_time.returncode, 0)
            self.assertIn("invalid encode_seconds", bad_time.stderr)


if __name__ == "__main__":
    unittest.main()
