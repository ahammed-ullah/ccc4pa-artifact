#!/usr/bin/env python3
"""Print one CSV row per graph after the Slurm size search."""

import argparse
import csv
import json
from pathlib import Path
import sys


def graph_names(path):
    for line in path.read_text(encoding="utf-8").splitlines():
        name = line.strip()
        if name and not name.startswith("#"):
            yield name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--graphs", type=Path, default=Path(__file__).with_name("graphs.txt"))
    args = parser.parse_args()

    writer = csv.writer(sys.stdout)
    writer.writerow(("graph", "status", "ccf_bytes", "webgraph_bytes", "perm_bytes",
                     "ratio_to_ccf", "difference_bytes", "candidate", "order",
                     "window", "max_ref_count",
                     "min_interval_length", "bvgraphz", "outdegrees", "references",
                     "blocks", "intervals", "residuals", "successful_trials",
                     "failed_trials"))
    for name in graph_names(args.graphs):
        path = args.output_dir / name / "manifest.json"
        if not path.is_file():
            writer.writerow((name, "missing"))
            continue
        with path.open(encoding="utf-8") as source:
            report = json.load(source)
        if report.get("search_version") != 3:
            writer.writerow((name, "wrong_search_version"))
            continue
        selected = report["selected"]
        settings = next(item for item in report["candidate_settings"]
                        if item["id"] == selected["id"])
        attempts = report["attempts"]
        writer.writerow((
            name, "complete", selected["target_ccf_bytes"], selected["runtime_bytes"],
            selected["files"].get("perm", {}).get("bytes", 0),
            f"{selected['ratio_to_ccf']:.6f}", selected["difference_from_ccf_bytes"],
            selected["id"], selected["order"], settings["window"], settings["max_ref_count"],
            settings["min_interval_length"], settings["bvgraphz"],
            settings["outdegrees"], settings["references"], settings["blocks"],
            settings["intervals"], settings["residuals"],
            sum(item["status"] == "success" for item in attempts),
            sum(item["status"] == "failed" for item in attempts),
        ))


if __name__ == "__main__":
    main()
