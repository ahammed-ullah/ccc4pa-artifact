#!/usr/bin/env python3
"""Export the encoder time of each final Zuckerli bundle as CSV."""

import argparse
import csv
import json
import math
from pathlib import Path
import sys


def graph_names(path):
    for line in path.read_text(encoding="utf-8").splitlines():
        name = line.strip()
        if name and not name.startswith("#"):
            yield name


def final_time(directory, graph):
    with (directory / "manifest.json").open(encoding="utf-8") as stream:
        manifest = json.load(stream)

    if manifest["graph"] != graph:
        raise ValueError("manifest names a different graph")
    order = manifest["order"]
    if order not in ("original", "bfs-llp"):
        raise ValueError("unknown vertex order")
    extensions = ("zk", "zki") + (("perm",) if order == "bfs-llp" else ())
    files = manifest["files"]
    if set(files) != set(extensions):
        raise ValueError("bundle file list differs from vertex order")

    total = 0
    for extension in extensions:
        info = files[extension]
        size = info["bytes"]
        path = directory / (graph + "." + extension)
        if type(size) is not int or size <= 0 or path.stat().st_size != size:
            raise ValueError("bundle file size differs from manifest: " + str(path))
        total += size
    if manifest["runtime_bytes"] != total:
        raise ValueError("bundle total differs from file sizes")

    seconds = manifest["encode_seconds"]
    if type(seconds) not in (int, float) or not math.isfinite(seconds) or seconds <= 0:
        raise ValueError("invalid encode_seconds")
    return order, total, seconds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--graphs", type=Path,
                        default=Path(__file__).with_name("graphs.txt"))
    args = parser.parse_args()

    writer = csv.writer(sys.stdout)
    writer.writerow(("graph", "status", "order", "bundle_bytes", "encode_seconds"))
    for graph in graph_names(args.graphs):
        directory = args.output_dir / graph
        if not (directory / "manifest.json").is_file():
            writer.writerow((graph, "missing", "", "", ""))
            continue
        try:
            order, size, seconds = final_time(directory, graph)
        except (KeyError, TypeError, ValueError, OSError) as error:
            raise ValueError("invalid final bundle for {}: {}".format(graph, error))
        writer.writerow((graph, "complete", order, size, seconds))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print("summarize_times: {}".format(error), file=sys.stderr)
        sys.exit(1)
