#!/usr/bin/env python3
"""Export the sum of successful WebGraph candidate times for each graph.

Each successful attempt in the final manifest has an encoder time and an .ef
index time. Earlier resume snapshots in attempt_history repeat some successful
attempts, so only the final attempts list contributes to the sum. Failed,
timed-out, and cancelled work has no complete timing in that list.
"""

import argparse
import csv
from decimal import Decimal
import json
import math
from pathlib import Path
import sys


def graph_names(path):
    for line in path.read_text(encoding="utf-8").splitlines():
        name = line.strip()
        if name and not name.startswith("#"):
            yield name


def successful_time(directory):
    with (directory / "manifest.json").open(encoding="utf-8") as source:
        manifest = json.load(source)
    if manifest.get("search_version") != 3:
        return None

    selected_id = manifest["selected"]["id"]
    successes = [attempt for attempt in manifest["attempts"]
                 if attempt["status"] == "success"]
    ids = [attempt["id"] for attempt in successes]
    if not successes or len(ids) != len(set(ids)) or ids.count(selected_id) != 1:
        raise ValueError("successful attempts do not identify the selected candidate in "
                         + str(directory))

    encode = Decimal(0)
    index = Decimal(0)
    for attempt in successes:
        for field in ("encode_seconds", "index_seconds"):
            value = attempt[field]
            if (type(value) not in (int, float) or
                    not math.isfinite(value) or value <= 0):
                raise ValueError("invalid " + field + " for " + attempt["id"]
                                 + " in " + str(directory))
        encode += Decimal(str(attempt["encode_seconds"]))
        index += Decimal(str(attempt["index_seconds"]))
    return len(successes), encode, index, encode + index


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--graphs", type=Path,
                        default=Path(__file__).with_name("graphs.txt"))
    args = parser.parse_args()

    writer = csv.writer(sys.stdout)
    writer.writerow(("graph", "status", "successful_trials", "encode_seconds",
                     "index_seconds", "successful_attempt_seconds"))
    for graph in graph_names(args.graphs):
        directory = args.output_dir / graph
        if not (directory / "manifest.json").is_file():
            writer.writerow((graph, "missing", "", "", "", ""))
            continue
        result = successful_time(directory)
        if result is None:
            writer.writerow((graph, "wrong_search_version", "", "", "", ""))
        else:
            writer.writerow((graph, "complete") + result)


if __name__ == "__main__":
    try:
        main()
    except (OSError, KeyError, TypeError, ValueError) as error:
        print("summarize_search_times: " + str(error), file=sys.stderr)
        sys.exit(1)
