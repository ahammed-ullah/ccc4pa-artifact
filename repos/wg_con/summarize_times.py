#!/usr/bin/env python3
"""Export the selected WebGraph attempt's recorded encoding and index times.

The time for an earlier BFS+LLP ordering trial and the other search attempts
are not included. The final bundle files must still match the manifest.
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


def selected_attempt(directory, graph):
    path = directory / "manifest.json"
    with path.open(encoding="utf-8") as source:
        manifest = json.load(source)
    if manifest.get("search_version") != 3:
        return (graph, "wrong_search_version", "", "", "", "", "", "")

    selected = manifest["selected"]
    matches = [item for item in manifest["attempts"]
               if item.get("id") == selected["id"] and item.get("status") == "success"]
    if len(matches) != 1:
        raise ValueError("expected one successful selected attempt in " + str(path))
    winner = matches[0]
    if (winner["runtime_bytes"] != selected["runtime_bytes"] or
            winner["order"] != selected["order"]):
        raise ValueError("selected attempt differs from final bundle in " + str(path))

    extensions = ("graph", "properties", "ef")
    if selected["order"] == "bfs-llp":
        extensions += ("perm",)
    elif selected["order"] != "original":
        raise ValueError("unknown selected order in " + str(path))
    if set(selected["files"]) != set(extensions) or set(winner["file_bytes"]) != set(extensions):
        raise ValueError("selected file list differs from attempt in " + str(path))

    total_bytes = 0
    for extension in extensions:
        info = selected["files"][extension]
        file = directory / (graph + "." + extension)
        if (info["name"] != file.name or not file.is_file() or
                file.stat().st_size != info["bytes"] or
                winner["file_bytes"][extension] != info["bytes"]):
            raise ValueError("selected file differs from manifest: " + str(file))
        total_bytes += info["bytes"]
    if total_bytes != selected["runtime_bytes"]:
        raise ValueError("selected bundle size differs from manifest in " + str(path))

    encode = winner["encode_seconds"]
    index = winner["index_seconds"]
    for label, seconds in (("encode_seconds", encode), ("index_seconds", index)):
        if (isinstance(seconds, bool) or not isinstance(seconds, (int, float)) or
                not math.isfinite(seconds) or seconds <= 0):
            raise ValueError("invalid " + label + " in " + str(path))
    selected_seconds = Decimal(str(encode)) + Decimal(str(index))
    return (graph, "complete", selected["id"], selected["order"],
            total_bytes, encode, index, selected_seconds)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--graphs", type=Path,
                        default=Path(__file__).with_name("graphs.txt"))
    args = parser.parse_args()

    writer = csv.writer(sys.stdout)
    writer.writerow(("graph", "status", "selected_id", "order", "bundle_bytes",
                     "encode_seconds", "index_seconds", "selected_attempt_seconds"))
    for graph in graph_names(args.graphs):
        directory = args.output_dir / graph
        if not (directory / "manifest.json").is_file():
            writer.writerow((graph, "missing", "", "", "", "", "", ""))
        else:
            writer.writerow(selected_attempt(directory, graph))


if __name__ == "__main__":
    main()
