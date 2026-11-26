#!/usr/bin/env python3
"""Encode one CCF graph using the order selected by WebGraph.

This script runs exactly one Zuckerli setting. It records the input and output
files, then moves the completed bundle into format_zk/<graph>/.
"""

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time


SETTINGS = {
    "window": 128,
    "reference_depth": "unlimited",
    "degree_reference_chunk": 1,
    "zero_run_min": 3,
    "hybrid_code": [3, 1, 0],
    "reference_rounds": 2,
    "entropy_coder": "huffman",
}


def sha256(path):
    digest = hashlib.sha256()
    with open(str(path), "rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def file_info(path):
    return {"name": path.name, "bytes": path.stat().st_size,
            "sha256": sha256(path)}


def ccf_info(path):
    with open(str(path), "rb") as stream:
        header = stream.read(160)
    if len(header) != 160 or header[:8] != b"CCF\x00\r\n\x1a\n":
        raise ValueError("invalid CCF header: " + str(path))
    n, m = struct.unpack_from("<QQ", header, 24)
    return {"path": str(path), "n": n, "m": m,
            "graph_fingerprint_sha256": header[96:128].hex(),
            "file_bytes": path.stat().st_size,
            "file_checksum_sha256": header[128:160].hex()}


def webgraph_info(path, ccf, graph):
    with open(str(path), "r") as stream:
        manifest = json.load(stream)
    selected = manifest["selected"]
    order = selected["order"]
    if order not in ("original", "bfs-llp"):
        raise ValueError("unknown selected WebGraph order: " + str(order))
    wg_ccf = manifest["inputs"]["ccf"]
    if (wg_ccf["n"] != ccf["n"] or wg_ccf["m"] != ccf["m"] or
            wg_ccf["graph_fingerprint_sha256"] !=
            ccf["graph_fingerprint_sha256"] or
            wg_ccf.get("file_checksum_sha256") !=
            ccf["file_checksum_sha256"]):
        raise ValueError("WebGraph manifest was built from a different CCF")

    files = selected["files"]
    wg_bytes = selected["runtime_bytes"]
    if sum(piece["bytes"] for piece in files.values()) != wg_bytes:
        raise ValueError("selected WebGraph file sizes do not add up")
    if order == "bfs-llp":
        perm_path = path.parent / (graph + ".perm")
        perm = files.get("perm")
        if (perm is None or not perm_path.is_file() or
                perm_path.stat().st_size != 4 * ccf["n"] or
                file_info(perm_path)["sha256"] != perm["sha256"]):
            raise ValueError("selected WebGraph permutation is missing or changed")
    else:
        perm_path = None
        if "perm" in files:
            raise ValueError("original WebGraph order has a permutation")

    return {
        "manifest_path": str(path),
        "manifest_sha256": sha256(path),
        "selected_id": selected["id"],
        "selected_order": order,
        "selected_files": files,
        "runtime_bytes": wg_bytes,
    }, perm_path


def completed(directory, graph, inputs, tool, order):
    path = directory / "manifest.json"
    if not path.is_file():
        return False
    try:
        with open(str(path), "r") as stream:
            old = json.load(stream)
        if (old["settings"] != SETTINGS or old["inputs"] != inputs or
                old["tool"] != tool or old["order"] != order):
            return False
        extensions = ["zk", "zki"] + (["perm"] if order == "bfs-llp" else [])
        if set(old["files"]) != set(extensions):
            return False
        for ext in extensions:
            file = directory / (graph + "." + ext)
            if not file.is_file() or file_info(file) != old["files"][ext]:
                return False
        return old["runtime_bytes"] == sum(
            old["files"][ext]["bytes"] for ext in extensions)
    except (KeyError, ValueError, OSError, TypeError):
        return False


def encode_one(args):
    graph = args.graph
    ccf_path = Path(args.ccf_dir).resolve() / (graph + ".ccf")
    wg_path = Path(args.wg_dir).resolve() / graph / "manifest.json"
    encoder = Path(args.encoder).resolve()
    if not encoder.is_file() or not os.access(str(encoder), os.X_OK):
        raise ValueError("encoder is not executable: " + str(encoder))

    ccf = ccf_info(ccf_path)
    wg, perm_path = webgraph_info(wg_path, ccf, graph)
    inputs = {"ccf": ccf, "webgraph": wg}
    tool = {"path": str(encoder), "sha256": sha256(encoder)}
    order = wg["selected_order"]
    directory = Path(args.output_dir).resolve() / graph
    directory.mkdir(parents=True, exist_ok=True)

    # submit.sh has exactly one array task per graph. A killed task may leave
    # an unpublished stage; clear those before accepting or rebuilding output.
    for old_stage in directory.glob(".stage-*"):
        if old_stage.is_dir():
            shutil.rmtree(str(old_stage))

    if completed(directory, graph, inputs, tool, order):
        print("{}: encoded bundle is already complete ({} bytes)".format(
            graph, json.loads((directory / "manifest.json").read_text())["runtime_bytes"]))
        return

    stage = Path(tempfile.mkdtemp(prefix=".stage-", dir=str(directory)))
    try:
        basename = stage / graph
        staged_perm = stage / (graph + ".perm")
        extra = []
        if perm_path is not None:
            shutil.copyfile(str(perm_path), str(staged_perm))
            extra = ["--perm", str(staged_perm)]

        print("{}: encoding order={} W=128 R=unlimited".format(graph, order),
              flush=True)
        start = time.monotonic()
        subprocess.run([str(encoder), "encode", str(ccf_path), str(basename)] +
                       extra, check=True)
        encode_seconds = time.monotonic() - start

        extensions = ["zk", "zki"] + (["perm"] if perm_path else [])
        files = {ext: file_info(stage / (graph + "." + ext))
                 for ext in extensions}
        runtime_bytes = sum(piece["bytes"] for piece in files.values())
        manifest = {
            "schema_version": 1,
            "graph": graph,
            "settings": SETTINGS,
            "order": order,
            "inputs": inputs,
            "tool": tool,
            "files": files,
            "runtime_bytes": runtime_bytes,
            "comparison_bytes": {
                "ccf": ccf["file_bytes"],
                "webgraph": wg["runtime_bytes"],
                "zuckerli": runtime_bytes,
            },
            "encode_seconds": encode_seconds,
        }
        staged_manifest = stage / "manifest.json"
        with open(str(staged_manifest), "w") as stream:
            json.dump(manifest, stream, indent=2, sort_keys=True)
            stream.write("\n")

        # The manifest is the completion marker. If promotion is interrupted,
        # the next run rebuilds the bundle rather than accepting mixed files.
        final_manifest = directory / "manifest.json"
        if final_manifest.exists():
            final_manifest.unlink()
        for ext in ("zk", "zki"):
            os.replace(str(stage / (graph + "." + ext)),
                       str(directory / (graph + "." + ext)))
        final_perm = directory / (graph + ".perm")
        if perm_path is not None:
            os.replace(str(staged_perm), str(final_perm))
        elif final_perm.exists():
            final_perm.unlink()
        os.replace(str(staged_manifest), str(final_manifest))
        print("{}: complete, {} bytes (CCF {}, WebGraph {})".format(
            graph, runtime_bytes, ccf["file_bytes"], wg["runtime_bytes"]),
            flush=True)
    finally:
        shutil.rmtree(str(stage))


def run(args):
    if not re.match(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$", args.graph):
        raise ValueError("invalid graph name")
    directory = Path(args.output_dir).resolve() / args.graph
    directory.mkdir(parents=True, exist_ok=True)
    # A second submission must not remove an active task's staging directory.
    # flock is released by the OS when a task exits or Slurm kills it.
    with open(str(directory / ".run.lock"), "a+") as lock:
        try:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("another run for {} is active".format(args.graph))
        encode_one(args)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--graph", required=True)
    parser.add_argument("--ccf-dir", required=True)
    parser.add_argument("--wg-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--encoder", required=True)
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print("zk_con run: {}".format(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
