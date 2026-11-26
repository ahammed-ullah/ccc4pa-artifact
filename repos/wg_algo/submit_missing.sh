#!/bin/bash
# Submit only the runs needed to reach five reports per graph and algorithm.
# Run this after earlier jobs have finished: queued jobs have no report yet.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
dry_run=0
decoder=bulk
retry_partial=0
time_override=
retry_config=
usage='Usage: submit_missing.sh [--dry-run] [--decoder iterator|bulk] [--retry-partial] [--time HH:MM:SS] [--config config.toml] [all|matching|bfs|dfs|wvc|mvm] [report_directory]'
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run) dry_run=1; shift ;;
        --retry-partial) retry_partial=1; shift ;;
        --decoder)
            [[ $# -ge 2 ]] || { echo "$usage" >&2; exit 2; }
            decoder=$2
            shift 2 ;;
        --time)
            [[ $# -ge 2 ]] || { echo "$usage" >&2; exit 2; }
            time_override=$2
            shift 2 ;;
        --config)
            [[ $# -ge 2 ]] || { echo "$usage" >&2; exit 2; }
            retry_config=$2
            shift 2 ;;
        *) break ;;
    esac
done
case "$decoder" in
    iterator|bulk) ;;
    *) echo "Unknown decoder: $decoder" >&2; exit 2 ;;
esac

if [[ $# -gt 2 ]]; then
    echo "$usage" >&2
    exit 2
fi

choice=${1:-all}
case "$choice" in
    all) algorithms=(matching bfs dfs wvc) ;;
    matching|bfs|dfs|wvc|mvm) algorithms=("$choice") ;;
    *) echo "Unknown algorithm: $choice" >&2; exit 2 ;;
esac
if [[ "$choice" != mvm && ( -n "$retry_config" || "$retry_partial" == 1 ) ]]; then
    echo "--config and --retry-partial only apply to mvm" >&2
    exit 2
fi

# On Negishi the reports are written in wg_algo, beside this script. The
# directory argument also lets us inspect a copied set such as evals_app/wg.
report_dir=$(cd -- "${2:-$script_dir}" && pwd)
cd "$script_dir"
if (( !dry_run )) && [[ "$report_dir" != "$script_dir" ]]; then
    echo "Submit using the live reports in $script_dir; an external directory is only for --dry-run" >&2
    exit 2
fi

if [[ "$choice" == mvm ]]; then
    # A seed, not a filename suffix, identifies an independent RAMA run.
    # A partial report is an observed run unless --retry-partial is requested.
    mvm_config=$retry_config
    if [[ -z "$mvm_config" && "$decoder" == iterator ]]; then
        mvm_config=config/iterator.toml
    fi
    mvm_reports=$(python3 - "$report_dir" "$decoder" "$mvm_config" "$script_dir" <<'PY'
import glob
import json
import os
import re
import sys

directory, decoder, config_arg, script_dir = sys.argv[1:]
expected_config = None
relative_config = None
if config_arg:
    expected_config = os.path.abspath(os.path.join(script_dir, config_arg))
    relative_config = os.path.relpath(expected_config, script_dir)
    if relative_config == ".." or relative_config.startswith(".." + os.sep):
        relative_config = None
    try:
        with open(expected_config) as source:
            config_text = source.read()
    except OSError as error:
        sys.exit("Cannot read retry config {}: {}".format(expected_config, error))
    config_decoder = "bulk"
    for line in config_text.splitlines():
        match = re.match(r'^\s*decoder\s*=\s*"(bulk|iterator)"\s*(?:#.*)?$', line)
        if match:
            config_decoder = match.group(1)
    if config_decoder != decoder:
        sys.exit("Retry config decoder is {}; pass --decoder {}".format(
            config_decoder, config_decoder))

def same_config(path):
    if expected_config is None:
        return path is None
    if not isinstance(path, str):
        return False
    normalized = os.path.abspath(os.path.join(script_dir, path))
    if normalized == expected_config:
        return True
    # Copied Negishi reports have a different absolute repository prefix.
    # A config stored within the repo can still be identified by its path
    # relative to wg_algo, such as config/iterator.toml.
    return relative_config is not None and normalized.endswith(
        os.sep + os.path.basename(script_dir) + os.sep + relative_config)

states = {}
for path in glob.glob(os.path.join(directory, "*.json")):
    if not os.path.basename(path).startswith("mvm-"):
        continue
    try:
        with open(path) as source:
            report = json.load(source)
    except (OSError, ValueError) as error:
        sys.exit("Cannot read report {}: {}".format(path, error))
    if report.get("representation") != "webgraph" or report.get("algorithm") != "mvm":
        continue
    parameters = report.get("parameters", {})
    if parameters.get("decoder") != decoder:
        continue
    if not same_config(report.get("config_path")):
        continue
    graph = report.get("input", {}).get("graph_name")
    seed = parameters.get("mvm_seed")
    status = report.get("mvm_execution", {}).get("status")
    if not graph or type(seed) is not int or \
            status not in ("complete", "time_limit_partial"):
        sys.exit("Invalid MVM graph, seed, or status in {}".format(path))
    if seed not in range(1, 6):
        continue
    key = (graph, seed)
    if states.get(key) != "complete":
        states[key] = status

for (graph, seed), status in sorted(states.items()):
    print("{}\t{}\t{}".format(graph, seed, status))
PY
)
    if (( !dry_run )); then mkdir -p logs; fi
    total=0
    while IFS= read -r graph || [[ -n "$graph" ]]; do
        graph=${graph%$'\r'}
        [[ -z "$graph" || "$graph" == \#* ]] && continue
        for ((run=1; run<=5; ++run)); do
            status=$(awk -F '\t' -v graph="$graph" -v seed="$run" '
                $1 == graph && $2 == seed { print $3; exit }
            ' <<< "$mvm_reports")
            status=${status:-missing}
            if [[ "$status" == complete ]] ||
               { [[ "$status" == time_limit_partial ]] && (( !retry_partial )); }; then
                continue
            fi
            printf 'mvm %s (%s), seed %d: %s\n' "$graph" "$decoder" "$run" "$status"
            total=$((total + 1))
            if (( !dry_run )); then
                options=(--job-name="mvm-${graph}-retry-${run}"
                         --output="logs/mvm-${graph}-retry-${run}-%j.out"
                         --signal=B:USR1@600 --export="ALL,MVM_SEED=$run")
                if [[ -n "$time_override" ]]; then options+=(--time="$time_override"); fi
                if [[ -n "$mvm_config" ]]; then
                    sbatch "${options[@]}" run.sh mvm "$graph" "$mvm_config"
                else
                    sbatch "${options[@]}" run.sh mvm "$graph"
                fi
            fi
        done
    done < graphs.txt
    if (( dry_run )); then
        printf 'Would submit %d jobs.\n' "$total"
    else
        printf 'Submitted %d jobs.\n' "$total"
    fi
    exit 0
fi

# Report names do not identify the decoder. Read the value recorded in each
# report; older reports without this field used the iterator decoder.
report_counts=$(python3 - "$report_dir" "$decoder" <<'PY'
import collections
import glob
import json
import os
import sys

directory, decoder = sys.argv[1:]
counts = collections.Counter()
for path in glob.glob(os.path.join(directory, "*.json")):
    name = os.path.basename(path)
    if not name.startswith(("matching-", "bfs-", "dfs-", "wvc-")):
        continue
    try:
        with open(path) as source:
            report = json.load(source)
    except (OSError, ValueError) as error:
        sys.exit("Cannot read report {}: {}".format(path, error))
    if report.get("representation") != "webgraph":
        continue
    mode = report.get("parameters", {}).get("decoder", "iterator")
    if mode != decoder:
        continue
    algorithm = report.get("algorithm")
    graph = report.get("input", {}).get("graph_name")
    if algorithm in ("matching", "bfs", "dfs", "wvc") and graph:
        counts[algorithm, graph] += 1

for (algorithm, graph), count in sorted(counts.items()):
    print("{}\t{}\t{}".format(algorithm, graph, count))
PY
)

# An empty or wrong directory would otherwise submit the entire experiment.
if [[ -z "$report_counts" ]]; then
    echo "No $decoder algorithm reports found in $report_dir" >&2
    exit 1
fi

if (( !dry_run )); then mkdir -p logs; fi
total=0
for algorithm in "${algorithms[@]}"; do
    while IFS= read -r graph || [[ -n "$graph" ]]; do
        graph=${graph%$'\r'}
        [[ -z "$graph" || "$graph" == \#* ]] && continue
        existing=$(awk -F '\t' -v algorithm="$algorithm" -v graph="$graph" '
            $1 == algorithm && $2 == graph { print $3; found = 1; exit }
            END { if (!found) print 0 }
        ' <<< "$report_counts")
        needed=$((5 - existing))
        if (( needed <= 0 )); then continue; fi
        printf '%s %s (%s): %d/5 reports, submit %d\n' \
            "$algorithm" "$graph" "$decoder" "$existing" "$needed"
        total=$((total + needed))
        for ((run=1; run<=needed; ++run)); do
            if (( !dry_run )); then
                options=(--job-name="${algorithm}-${graph}-retry-${run}"
                         --output="logs/${algorithm}-${graph}-retry-${run}-%j.out")
                if [[ -n "$time_override" ]]; then options+=(--time="$time_override"); fi
                if [[ "$decoder" == iterator ]]; then
                    sbatch "${options[@]}" run.sh "$algorithm" "$graph" config/iterator.toml
                else
                    sbatch "${options[@]}" run.sh "$algorithm" "$graph"
                fi
            fi
        done
    done < graphs.txt
done
if (( dry_run )); then
    printf 'Would submit %d jobs.\n' "$total"
else
    printf 'Submitted %d jobs.\n' "$total"
fi
