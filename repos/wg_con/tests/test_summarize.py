"""CSV summary of completed WebGraph bundles."""

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
import summarize


def write_manifest(output, name, order, pieces, search_version=3):
    directory = output / name
    directory.mkdir(parents=True)
    total = sum(pieces.values())
    selected = {
        "id": order.replace("-", "_") + "_w32",
        "order": order,
        "runtime_bytes": total,
        "target_ccf_bytes": 100,
        "difference_from_ccf_bytes": total - 100,
        "ratio_to_ccf": total / 100,
        "files": {extension: {"bytes": size} for extension, size in pieces.items()},
    }
    settings = {
        "id": selected["id"], "window": 32, "max_ref_count": "unlimited",
        "min_interval_length": 4, "bvgraphz": False,
        "outdegrees": "gamma", "references": "gamma", "blocks": "gamma",
        "intervals": "gamma", "residuals": "zeta3",
    }
    manifest = {
        "search_version": search_version,
        "selected": selected,
        "candidate_settings": [settings],
        "attempts": [{"status": "success"}, {"status": "failed"}],
    }
    (directory / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")


class SummaryTests(unittest.TestCase):
    def test_v3_summary_counts_permutation_in_webgraph_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "format_wg"
            write_manifest(output, "reordered", "bfs-llp", {
                "graph": 50, "properties": 11, "ef": 8, "perm": 16,
            })
            write_manifest(output, "original", "original", {
                "graph": 50, "properties": 11, "ef": 8,
            })
            write_manifest(output, "old", "original", {
                "graph": 50, "properties": 11, "ef": 8,
            }, search_version=2)
            graphs = root / "graphs.txt"
            graphs.write_text("reordered\noriginal\nold\nmissing\n", encoding="utf-8")

            stdout = io.StringIO()
            with mock.patch.object(sys, "argv", [
                    "summarize.py", "--output-dir", str(output),
                    "--graphs", str(graphs)]), contextlib.redirect_stdout(stdout):
                summarize.main()
            rows = {row["graph"]: row for row in csv.DictReader(io.StringIO(stdout.getvalue()))}

            self.assertEqual(rows["reordered"]["status"], "complete")
            self.assertEqual(rows["reordered"]["webgraph_bytes"], "85")
            self.assertEqual(rows["reordered"]["perm_bytes"], "16")
            self.assertEqual(rows["reordered"]["order"], "bfs-llp")
            self.assertEqual(rows["reordered"]["ratio_to_ccf"], "0.850000")
            self.assertEqual(rows["original"]["webgraph_bytes"], "69")
            self.assertEqual(rows["original"]["perm_bytes"], "0")
            self.assertEqual(rows["original"]["order"], "original")
            self.assertEqual(rows["old"]["status"], "wrong_search_version")
            self.assertEqual(rows["missing"]["status"], "missing")


if __name__ == "__main__":
    unittest.main()
