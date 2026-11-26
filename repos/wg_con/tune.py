#!/usr/bin/env python3
"""Choose the smallest completed WebGraph bundle from a short CCF-based search.

The candidate list below is deliberately easy to edit. A job keeps its best
completed encoding in its graph directory, so an interrupted search can resume.
"""

import argparse
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time


HEADER_BYTES = 160
CCF_MAGIC = b"CCF\0\r\n\x1a\n"
RUNTIME_EXTENSIONS = ("graph", "properties", "ef")
SEARCH_VERSION = 3

# Edit these lines to extend the search. A window is the number of preceding
# vertices whose adjacency lists the compressor may reference. By default,
# reference depth is unlimited; the timeout bounds encoding time instead.
# Each entry may also set "interval", "codes" (a dictionary of code changes),
# "max_ref_count", "bvgraphz", "timeout_seconds", or an explicit "id" when
# trying two settings with the same order/window. Extra entries inherit the
# smaller W32 result's order;
# each extra runs only if the preceding extra completed successfully.
INITIAL_CANDIDATES = (
    {"order": "original", "window": 16},
    {"order": "original", "window": 32},
    {"order": "bfs-llp", "window": 16},
    {"order": "bfs-llp", "window": 32},
)
EXTRA_CANDIDATES = ({"window": 64}, {"window": 128})
MIN_INTERVAL_LENGTH = 4
CANDIDATE_TIMEOUT_SECONDS = 6 * 60 * 60
DEADLINE_RESERVE_SECONDS = 10 * 60

START_CODES = {
    "outdegrees": "gamma",
    "references": "gamma",
    "blocks": "gamma",
    "intervals": "gamma",
    "residuals": "zeta3",
}
CODE_FIELDS = ("outdegrees", "references", "blocks", "intervals", "residuals")
VALID_CODES = {"unary", "gamma", "delta"} | {
    "zeta{}".format(value) for value in range(1, 8)
} | {"pi{}".format(value) for value in range(1, 5)}
SPEC_FIELDS = {
    "id", "order", "window", "interval", "codes", "max_ref_count",
    "bvgraphz", "timeout_seconds",
}


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_header(path):
    """Read the documented CCF v1 header without decoding its records."""
    if path.suffix.lower() != ".ccf":
        raise ValueError(f"Expected a .ccf file: {path}")
    with path.open("rb") as source:
        raw = source.read(HEADER_BYTES)
    if len(raw) != HEADER_BYTES:
        raise ValueError(f"{path} is shorter than the {HEADER_BYTES}-byte header")

    fields = struct.unpack_from("<8sIIII9Q", raw)
    magic, version, header_bytes, encoding, flags = fields[:5]
    n, m, records, entries, offsets_at, offset_count, payload_at, payload_bytes, file_bytes = fields[5:]
    if magic != CCF_MAGIC or (version, header_bytes, encoding, flags) != (1, 160, 1, 0):
        raise ValueError(f"{path} is not a supported CCF v1 file")
    if (offsets_at != HEADER_BYTES or offset_count != records + 1 or
            payload_at != HEADER_BYTES + 8 * offset_count or
            file_bytes != payload_at + payload_bytes or
            file_bytes != path.stat().st_size):
        raise ValueError(f"{path} has inconsistent positions or physical size")
    return {
        "n": n,
        "m": m,
        "records": records,
        "entries": entries,
        "file_bytes": file_bytes,
        "graph_fingerprint_sha256": raw[96:128].hex(),
        "file_checksum_sha256": raw[128:160].hex(),
    }


def read_graph_counts(properties):
    """Read the two Java-properties fields used by the WebGraph loader."""
    counts = {}
    with properties.open("r", encoding="latin-1") as source:
        for line in source:
            match = re.match(r"^\s*(nodes|arcs)\s*(?:=|:|\s)\s*(\d+)\s*$", line)
            if match:
                key, value = match.groups()
                if key in counts:
                    raise ValueError(f"Duplicate {key} property in {properties}")
                counts[key] = int(value)
    if set(counts) != {"nodes", "arcs"}:
        raise ValueError(f"Missing nodes or arcs property in {properties}")
    return counts


def candidate_settings(ident, order, window, interval, codes, max_ref_count, bvgraphz):
    return {
        "id": ident,
        "order": order,
        "window": window,
        "max_ref_count": max_ref_count,
        "min_interval_length": interval,
        "bvgraphz": bvgraphz,
        **codes,
    }


def setting_from_spec(spec, inherited_order=None):
    unknown = set(spec) - SPEC_FIELDS
    if unknown:
        raise ValueError("Unknown candidate fields: " + ", ".join(sorted(unknown)))
    order = spec.get("order", inherited_order)
    if order not in ("original", "bfs-llp"):
        raise ValueError("Candidate order must be original or bfs-llp")
    window = spec["window"]
    interval = spec.get("interval", MIN_INTERVAL_LENGTH)
    if type(window) is not int or window < 0:
        raise ValueError("Candidate window must be a nonnegative integer")
    if type(interval) is not int or interval < 0:
        raise ValueError("Candidate interval must be a nonnegative integer")
    max_ref_count = spec.get("max_ref_count", "unlimited")
    if (max_ref_count != "unlimited" and
            (type(max_ref_count) is not int or max_ref_count < 0)):
        raise ValueError("Candidate max_ref_count must be unlimited or a nonnegative integer")
    bvgraphz = spec.get("bvgraphz", False)
    if type(bvgraphz) is not bool:
        raise ValueError("Candidate bvgraphz must be true or false")
    if "timeout_seconds" in spec:
        value = spec["timeout_seconds"]
        if type(value) not in (int, float) or value <= 0:
            raise ValueError("Candidate timeout_seconds must be positive")
    code_changes = spec.get("codes", {})
    if not isinstance(code_changes, dict):
        raise ValueError("Candidate codes must be a dictionary")
    codes = {**START_CODES, **code_changes}
    if set(codes) != set(CODE_FIELDS) or any(code not in VALID_CODES for code in codes.values()):
        raise ValueError("Candidate has an unknown code field or code")
    ident = spec.get("id", candidate_id(order, window))
    if not isinstance(ident, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", ident):
        raise ValueError("Candidate ID must use letters, digits, underscores, or hyphens")
    return candidate_settings(
        ident, order, window, interval, codes, max_ref_count, bvgraphz,
    )


def rank(record):
    """Smallest complete runtime bundle, then deterministic candidate ID."""
    return record["runtime_bytes"], record["id"]


def tool_info(name):
    path = shutil.which(name)
    if path is None:
        raise ValueError(f"Executable not found: {name}")
    path = Path(path).resolve()
    try:
        version = subprocess.run(
            [str(path), "--version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            universal_newlines=True,
            timeout=30, check=False,
        )
        version_text = (version.stdout or version.stderr).strip() if version.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired):
        version_text = None
    return {"path": str(path), "sha256": sha256_file(path), "version": version_text}


def run_logged(command, log_path, timeout=None):
    started = time.monotonic()
    with log_path.open("wb") as log:
        completed = subprocess.run(
            command, stdout=log, stderr=subprocess.STDOUT,
            timeout=timeout, check=False,
        )
    if completed.returncode:
        with log_path.open("rb") as log:
            log.seek(max(0, log_path.stat().st_size - 8192))
            tail = log.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"Command failed ({completed.returncode}): {' '.join(command)}\n{tail}")
    return round(time.monotonic() - started, 6)


def atomic_json(path, value):
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8") as destination:
        json.dump(value, destination, indent=2)
        destination.write("\n")
    os.replace(str(temporary), str(path))


def bundle_extensions(order):
    return RUNTIME_EXTENSIONS + (("perm",) if order == "bfs-llp" else ())


def validate_existing(directory, expected):
    """A finished search is reusable only with the same inputs and policy."""
    with (directory / "manifest.json").open("r", encoding="utf-8") as source:
        manifest = json.load(source)
    if any(manifest.get(key) != value for key, value in expected.items()):
        raise ValueError("Completed search has different inputs, tools, or candidates")
    selected = manifest["selected"]
    files = selected["files"]
    if set(files) != set(bundle_extensions(selected["order"])):
        raise ValueError("Completed WebGraph bundle is incomplete")
    for extension, info in files.items():
        path = directory / info["name"]
        if (info["name"] != directory.name + "." + extension or
                path.stat().st_size != info["bytes"] or
                sha256_file(path) != info["sha256"]):
            raise ValueError("Completed WebGraph bundle differs from its manifest")
    if sum(info["bytes"] for info in files.values()) != selected["runtime_bytes"]:
        raise ValueError("Completed bundle size differs from its manifest")
    return manifest


def has_unfinished_trials(manifest):
    return any(item["status"] in ("timeout", "skipped_deadline")
               for item in manifest["attempts"])


def resume_finalized(directory, graph_name, manifest, expected):
    """Seed a new checkpoint from a finalized search without copying its bundle.

    The old manifest and bundle remain readable at the top level until the new
    search finalizes. A crash before state.json is written can repeat this setup;
    a crash afterward resumes from best/ and state.json.
    """
    selected = manifest["selected"]
    winners = [item for item in manifest["attempts"] if
               item["id"] == selected["id"] and item["status"] == "success"]
    if len(winners) != 1 or winners[0]["runtime_bytes"] != selected["runtime_bytes"]:
        raise ValueError("Selected candidate is missing from the completed attempts")

    for name in ("current", "best", ".best_previous"):
        path = directory / name
        if path.exists():
            shutil.rmtree(path)
    order_cache = directory / "order.perm"
    if order_cache.exists():
        order_cache.unlink()

    best = directory / "best"
    best.mkdir()
    for extension, info in selected["files"].items():
        os.link(str(directory / info["name"]),
                str(best / (graph_name + "." + extension)))
    atomic_json(best / "record.json", winners[0])
    if selected["order"] == "bfs-llp":
        os.link(str(best / (graph_name + ".perm")), str(order_cache))

    retry_bfs_order = any(item["order"] == "bfs-llp" and item["status"] == "timeout"
                          for item in manifest["attempts"])
    retained_attempts = [
        item for item in manifest["attempts"]
        if item["status"] in ("success", "failed") or
        (item["status"] == "skipped_order" and not retry_bfs_order)
    ]
    state = {
        "expected": expected,
        "attempts": retained_attempts,
        "attempt_history": manifest.get("attempt_history", []) + manifest["attempts"],
        "selected_id": selected["id"],
        "phase": "running",
        "stop_reason": "completed",
    }
    save_state(directory, state)
    return state


def manifest_matches_state(manifest, state):
    """Distinguish a newly written manifest from the old incumbent manifest."""
    return (manifest["selected"]["id"] == state["selected_id"] and
            manifest["attempts"] == state["attempts"] and
            manifest.get("attempt_history", []) == state.get("attempt_history", []))


def clean_finalized(directory, graph_name, manifest):
    for name in ("current", "best", "previous", ".best_previous"):
        path = directory / name
        if path.exists():
            shutil.rmtree(path)
    state_path = directory / "state.json"
    if state_path.exists():
        state_path.unlink()
    order_cache = directory / "order.perm"
    if order_cache.exists():
        order_cache.unlink()
    if manifest["selected"]["order"] == "original":
        stale_permutation = directory / (graph_name + ".perm")
        if stale_permutation.exists():
            stale_permutation.unlink()


def is_previous_search(directory, graph_name, fingerprint):
    """Recognize an older completed result before moving it into previous/."""
    try:
        with (directory / "manifest.json").open("r", encoding="utf-8") as source:
            report = json.load(source)
        if report["schema_version"] != 1 or report["search_version"] not in (1, 2, 3):
            return False
        if report["inputs"]["ccf"]["graph_fingerprint_sha256"] != fingerprint:
            return False
        files = report["selected"]["files"]
        if not set(RUNTIME_EXTENSIONS).issubset(files):
            return False
        if set(files) - set(RUNTIME_EXTENSIONS) - {"perm"}:
            return False
        for extension, info in files.items():
            path = directory / info["name"]
            if (info["name"] != graph_name + "." + extension or
                    path.stat().st_size != info["bytes"] or
                    sha256_file(path) != info["sha256"]):
                return False
        return True
    except (OSError, KeyError, TypeError, json.JSONDecodeError, ValueError):
        return False


def save_state(directory, state):
    atomic_json(directory / "state.json", state)


def recorded_attempt(directory):
    try:
        with (directory / "record.json").open("r", encoding="utf-8") as source:
            record = json.load(source)
        if record["status"] != "success":
            return None
        for extension in bundle_extensions(record["order"]):
            if not (directory / (record["graph_name"] + "." + extension)).is_file():
                return None
        return record
    except (OSError, KeyError, TypeError, json.JSONDecodeError):
        return None


def repair_best(directory, state):
    """Resolve an interrupted best swap using the last durable state.json."""
    best = directory / "best"
    previous_best = directory / ".best_previous"
    selected_id = state["selected_id"]
    best_record = recorded_attempt(best) if best.exists() else None
    previous_record = recorded_attempt(previous_best) if previous_best.exists() else None

    if previous_best.exists():
        if previous_record and previous_record["id"] == selected_id:
            if best.exists():
                shutil.rmtree(best)
            previous_best.rename(best)
            best_record = previous_record
        elif best_record and best_record["id"] == selected_id:
            shutil.rmtree(previous_best)
        else:
            raise ValueError("Cannot recover interrupted best swap")

    if selected_id is None and best_record:
        if not any(item["id"] == best_record["id"] for item in state["attempts"]):
            state["attempts"].append(best_record)
        state["selected_id"] = best_record["id"]
        save_state(directory, state)
    elif selected_id is not None and state["phase"] != "finalizing":
        if best_record is None or best_record["id"] != selected_id:
            raise ValueError("Saved best bundle is missing or differs from state.json")

    current = directory / "current"
    if current.exists():
        shutil.rmtree(current)


def move_previous(directory, graph_name, fingerprint):
    """Keep an existing complete result until a new result is finalized."""
    previous = directory / "previous"
    if previous.exists():
        if not previous.is_dir() or previous.is_symlink():
            raise ValueError("Unrecognized previous result slot")
    elif (directory / "manifest.json").exists():
        if not is_previous_search(directory, graph_name, fingerprint):
            raise ValueError("Existing result is incomplete or for another graph")
        previous.mkdir()
    else:
        return

    # These moves are idempotent if interrupted between individual files.
    for extension in RUNTIME_EXTENSIONS + ("perm",):
        source = directory / (graph_name + "." + extension)
        if source.exists():
            source.rename(previous / source.name)
    old_manifest = directory / "manifest.json"
    if old_manifest.exists():
        old_manifest.rename(previous / old_manifest.name)
    if not is_previous_search(previous, graph_name, fingerprint):
        raise ValueError("Previous result could not be preserved")
    order_cache = directory / "order.perm"
    if order_cache.exists():
        order_cache.unlink()


def restore_previous(directory):
    previous = directory / "previous"
    for name in ("current", "best", ".best_previous"):
        path = directory / name
        if path.exists():
            shutil.rmtree(path)
    if previous.exists():
        for path in previous.iterdir():
            path.rename(directory / path.name)
        previous.rmdir()
    state_path = directory / "state.json"
    if state_path.exists():
        state_path.unlink()
    order_cache = directory / "order.perm"
    if order_cache.exists():
        order_cache.unlink()
    if not any(directory.iterdir()):
        directory.rmdir()


def candidate_id(order, window):
    return "{}_w{}".format(order.replace("-", "_"), window)


def selected_record(state):
    return next(
        (item for item in state["attempts"] if item["id"] == state["selected_id"]),
        None,
    )


def remaining_timeout(deadline_unix, candidate_limit=CANDIDATE_TIMEOUT_SECONDS):
    if deadline_unix is None:
        return candidate_limit
    available = deadline_unix - time.time() - DEADLINE_RESERVE_SECONDS
    return min(candidate_limit, available) if available > 0 else 0


def finalize(directory, graph_name, state, expected, target):
    best = directory / "best"
    selected = selected_record(state)
    if selected is None:
        raise RuntimeError("No completed candidate is available")
    if state["phase"] != "finalizing":
        state["phase"] = "finalizing"
        save_state(directory, state)

    files = {}
    for extension in bundle_extensions(selected["order"]):
        name = graph_name + "." + extension
        source = best / name
        destination = directory / name
        if source.exists():
            source.rename(destination)
        if not destination.is_file():
            raise ValueError("Missing selected bundle file: " + str(destination))
        files[extension] = {
            "name": name,
            "bytes": destination.stat().st_size,
            "sha256": sha256_file(destination),
        }
    size = sum(item["bytes"] for item in files.values())
    if size != selected["runtime_bytes"]:
        raise ValueError("Selected bundle size changed during finalization")
    manifest = {
        **expected,
        "candidate_settings": [
            {key: value for key, value in item.items() if key in (
                "id", "order", "window", "max_ref_count", "min_interval_length",
                "bvgraphz", "outdegrees", "references", "blocks", "intervals", "residuals",
            )} for item in state["attempts"]
        ],
        "attempts": state["attempts"],
        "search_stop_reason": state["stop_reason"],
        "runtime_size_definition": ".graph + .properties + .ef + .perm when reordered; .offsets is a build artifact",
        "selected": {
            "id": selected["id"],
            "order": selected["order"],
            "bvgraphz": selected["bvgraphz"],
            "runtime_bytes": size,
            "target_ccf_bytes": target,
            "difference_from_ccf_bytes": size - target,
            "ratio_to_ccf": size / target,
            "files": files,
        },
    }
    if "attempt_history" in state:
        manifest["attempt_history"] = state["attempt_history"]
    atomic_json(directory / "manifest.json", manifest)
    clean_finalized(directory, graph_name, manifest)
    return manifest


def tune(ccf_path, output_dir, encoder_name, webgraph_name, deadline_unix=None,
         resume_incomplete=False, candidate_timeout_seconds=None):
    if candidate_timeout_seconds is not None:
        if (not resume_incomplete or not math.isfinite(candidate_timeout_seconds) or
                candidate_timeout_seconds <= 0):
            raise ValueError("A positive candidate timeout override requires --resume-incomplete")
    candidate_limit = candidate_timeout_seconds or CANDIDATE_TIMEOUT_SECONDS
    ccf = read_header(ccf_path)
    graph_name = ccf_path.stem
    encoder = tool_info(encoder_name)
    webgraph = tool_info(webgraph_name)
    if deadline_unix is None:
        slurm_end = os.environ.get("SLURM_JOB_END_TIME")
        if slurm_end and slurm_end.isdigit():
            deadline_unix = int(slurm_end)
    expected = {
        "schema_version": 1,
        "search_version": SEARCH_VERSION,
        "selection_rule": "minimum complete runtime bundle bytes; ties: candidate ID",
        "candidate_policy": {
            "initial": [dict(item) for item in INITIAL_CANDIDATES],
            "extra": [dict(item) for item in EXTRA_CANDIDATES],
            "min_interval_length": MIN_INTERVAL_LENGTH,
            "defaults": {
                "max_ref_count": "unlimited",
                "bvgraphz": False,
                "codes": START_CODES,
            },
            "candidate_timeout_seconds": CANDIDATE_TIMEOUT_SECONDS,
        },
        "inputs": {"ccf": {"path": str(ccf_path), **ccf}},
        "tools": {"encoder": encoder, "webgraph": webgraph},
    }
    target = ccf["file_bytes"]
    output_dir.mkdir(parents=True, exist_ok=True)
    directory = output_dir / graph_name
    lock_path = output_dir / ("." + graph_name + ".lock")
    lock_fd = os.open(str(lock_path), os.O_CREAT | os.O_RDWR, 0o600)
    try:
        fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError as error:
        os.close(lock_fd)
        raise ValueError("Another search holds " + str(lock_path)) from error
    try:
        if directory.is_symlink():
            raise ValueError("Output is a symlink: " + str(directory))
        directory.mkdir(exist_ok=True)
        manifest_path = directory / "manifest.json"
        state_path = directory / "state.json"
        existing = None
        if manifest_path.exists():
            try:
                existing = validate_existing(directory, expected)
            except (OSError, KeyError, TypeError, json.JSONDecodeError, ValueError):
                pass
        if existing is not None and not state_path.exists():
            if resume_incomplete and has_unfinished_trials(existing):
                print("{}: resuming unfinished trials".format(graph_name),
                      file=sys.stderr, flush=True)
                resume_finalized(directory, graph_name, existing, expected)
            else:
                if resume_incomplete:
                    print("{}: no unfinished trials in manifest".format(graph_name),
                          file=sys.stderr, flush=True)
                clean_finalized(directory, graph_name, existing)
                return directory
        elif resume_incomplete and not state_path.exists():
            raise ValueError("No matching finalized, unfinished search to resume")

        if state_path.exists():
            with state_path.open("r", encoding="utf-8") as source:
                state = json.load(source)
            if state.get("expected") != expected:
                raise ValueError("Interrupted search has different inputs, tools, or candidates")
            if (existing is not None and state["phase"] == "finalizing" and
                    manifest_matches_state(existing, state)):
                clean_finalized(directory, graph_name, existing)
                return directory
            repair_best(directory, state)
            if state["phase"] == "finalizing":
                finalize(directory, graph_name, state, expected, target)
                return directory
        else:
            move_previous(directory, graph_name, ccf["graph_fingerprint_sha256"])
            # A new search must compute its own BFS+LLP order. Only a search
            # resumed from state.json may reuse a cached permutation.
            order_cache = directory / "order.perm"
            if order_cache.exists():
                order_cache.unlink()
            state = {
                "expected": expected,
                "attempts": [],
                "selected_id": None,
                "phase": "running",
                "stop_reason": "completed",
            }
            save_state(directory, state)

        def attempt(spec, inherited_order=None):
            setting = setting_from_spec(spec, inherited_order)
            ident = setting["id"]
            prior = next((item for item in state["attempts"] if item["id"] == ident), None)
            if prior is not None:
                if any(prior.get(key) != value for key, value in setting.items()):
                    raise ValueError("Candidate ID is reused with different settings: " + ident)
                return prior
            order, window = setting["order"], setting["window"]
            timeout = min(
                remaining_timeout(deadline_unix, candidate_limit),
                spec.get("timeout_seconds", candidate_limit),
            )
            if timeout <= 0:
                state["stop_reason"] = "deadline"
                record = {**setting, "status": "skipped_deadline", "time_limit_seconds": 0}
                state["attempts"].append(record)
                save_state(directory, state)
                return record

            print("{}: {} window={} references={} bvgraphz={}".format(
                graph_name, order, window, setting["max_ref_count"],
                setting["bvgraphz"],
            ), file=sys.stderr, flush=True)
            current = directory / "current"
            current.mkdir()
            basename = current / graph_name
            started = time.monotonic()
            try:
                command = [
                    encoder["path"], "--ccf", str(ccf_path), "--basename", str(basename),
                    "--order", order, "--window", str(window),
                    "--max-ref-count", str(setting["max_ref_count"]),
                    "--min-interval-length", str(setting["min_interval_length"]),
                ]
                if setting["bvgraphz"]:
                    command.append("--bvgraphz")
                if order == "bfs-llp":
                    command.extend(("--order-cache", str(directory / "order.perm")))
                for field in CODE_FIELDS:
                    command.extend(("--" + field, setting[field]))
                encode_seconds = run_logged(command, current / "encode.log", timeout)
                for extension in ("graph", "offsets", "properties"):
                    if not (current / (graph_name + "." + extension)).is_file():
                        raise RuntimeError("Encoder omitted ." + extension)
                counts = read_graph_counts(current / (graph_name + ".properties"))
                if counts != {"nodes": ccf["n"], "arcs": 2 * ccf["m"]}:
                    raise ValueError("WebGraph node/arc counts differ from CCF: " + str(counts))
                if order == "bfs-llp":
                    permutation = current / (graph_name + ".perm")
                    if not permutation.is_file() or permutation.stat().st_size != 4 * ccf["n"]:
                        raise ValueError("Reordered graph has no n-entry u32 permutation")
                elif (current / (graph_name + ".perm")).exists():
                    raise ValueError("Original-order graph unexpectedly has a permutation")
                left = timeout - (time.monotonic() - started)
                if left <= 0:
                    raise TimeoutError("Candidate time limit expired before index build")
                index_seconds = run_logged(
                    [webgraph["path"], "build", "ef", str(basename)],
                    current / "index.log", left,
                )
                if not (current / (graph_name + ".ef")).is_file():
                    raise RuntimeError("WebGraph did not write .ef")
                pieces = {
                    extension: (current / (graph_name + "." + extension)).stat().st_size
                    for extension in bundle_extensions(order)
                }
                record = {
                    **setting,
                    "graph_name": graph_name,
                    "status": "success",
                    "time_limit_seconds": round(timeout, 6),
                    "file_bytes": pieces,
                    "runtime_bytes": sum(pieces.values()),
                    "offsets_build_bytes": (current / (graph_name + ".offsets")).stat().st_size,
                    "encode_seconds": encode_seconds,
                    "index_seconds": index_seconds,
                }
                atomic_json(current / "record.json", record)
            except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired, TimeoutError) as error:
                status = "timeout" if isinstance(error, (subprocess.TimeoutExpired, TimeoutError)) else "failed"
                record = {**setting, "status": status,
                          "time_limit_seconds": round(timeout, 6),
                          "error": str(error)[-2048:]}
                print("  {}: {}".format(status, record["error"][-200:]), file=sys.stderr, flush=True)
                shutil.rmtree(current)
                state["attempts"].append(record)
                save_state(directory, state)
                return record

            previous_best = selected_record(state)
            state["attempts"].append(record)
            if previous_best is None or rank(record) < rank(previous_best):
                best = directory / "best"
                backup = directory / ".best_previous"
                if best.exists():
                    best.rename(backup)
                current.rename(best)
                state["selected_id"] = ident
                save_state(directory, state)
                if backup.exists():
                    shutil.rmtree(backup)
                print("  best={} bytes".format(record["runtime_bytes"]),
                      file=sys.stderr, flush=True)
            else:
                shutil.rmtree(current)
                save_state(directory, state)
            return record

        for spec in INITIAL_CANDIDATES:
            setting = setting_from_spec(spec)
            if (setting["order"] == "bfs-llp" and
                    not (directory / "order.perm").exists() and
                    any(item["order"] == "bfs-llp" and item["status"] in
                        ("failed", "timeout") for item in state["attempts"])):
                if not any(item["id"] == setting["id"] for item in state["attempts"]):
                    state["attempts"].append({**setting, "status": "skipped_order"})
                    save_state(directory, state)
                continue
            attempt(spec)

        at_32 = [item for item in state["attempts"] if
                 item["status"] == "success" and item["window"] == 32]
        if at_32:
            better_order = min(at_32, key=rank)["order"]
            preceding_succeeded = True
            for spec in EXTRA_CANDIDATES:
                if not preceding_succeeded:
                    break
                record = attempt(spec, better_order)
                preceding_succeeded = record["status"] == "success"

        if state["selected_id"] is None:
            restore_previous(directory)
            failures = "; ".join(item["id"] + ": " + item["status"] for item in state["attempts"])
            raise RuntimeError("No WebGraph candidate completed for {} ({})".format(graph_name, failures))
        finalize(directory, graph_name, state, expected, target)
        return directory
    finally:
        fcntl.flock(lock_fd, fcntl.LOCK_UN)
        os.close(lock_fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ccf", type=Path, required=True, help="final cover file used as graph input")
    parser.add_argument("--output-dir", type=Path, required=True, help="directory containing one subdirectory per graph")
    parser.add_argument("--encoder", required=True, help="path to wg_encode")
    parser.add_argument("--webgraph", required=True, help="path to the WebGraph CLI")
    parser.add_argument("--deadline-unix", type=float, help="optional job end time as a Unix timestamp")
    parser.add_argument("--resume-incomplete", action="store_true",
                        help="continue a finalized search with timed-out or deadline-skipped trials")
    parser.add_argument("--candidate-timeout-seconds", type=float,
                        help="per-trial time limit for this resume (default: six hours)")
    args = parser.parse_args()
    try:
        directory = tune(
            args.ccf.resolve(), args.output_dir.resolve(),
            args.encoder, args.webgraph, args.deadline_unix,
            args.resume_incomplete, args.candidate_timeout_seconds,
        )
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, "wg_con: {}\n".format(error))
    print(directory / "manifest.json")


if __name__ == "__main__":
    main()
