#!/bin/bash
# Submit the runs needed to reach five reports per graph and algorithm.
# Run this only after earlier jobs finish: queued jobs have no report yet.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
usage='Usage: submit_missing.sh [--dry-run] [--retry-partial] [--time D-HH:MM:SS|HH:MM:SS] [all|matching|bfs|dfs|wvc|mvm] [report_directory]'
dry_run=0
retry_partial=0
time_limit=
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run) dry_run=1; shift ;;
        --retry-partial) retry_partial=1; shift ;;
        --time)
            [[ $# -ge 2 ]] || { echo "$usage" >&2; exit 2; }
            time_limit=$2
            shift 2 ;;
        *) break ;;
    esac
done
if [[ -n "$time_limit" && ! "$time_limit" =~ ^([0-9]+-)?[0-9]+:[0-9][0-9]:[0-9][0-9]$ ]]; then
    echo "Invalid Slurm time: $time_limit" >&2
    exit 2
fi
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
if [[ "$choice" != mvm && "$retry_partial" == 1 ]]; then
    echo "--retry-partial only applies to mvm" >&2
    exit 2
fi

# Reports may have been moved into an evaluation directory. Count those and
# any newer reports still in zk_algo. Copies have the same Slurm job ID.
report_dir=$(cd -- "${2:-$script_dir}" && pwd)
cd "$script_dir"

# MVM repetitions are five distinct sampling seeds. A partial JSON is an
# observed run unless --retry-partial explicitly asks to rerun that seed.
if [[ "$choice" == mvm ]]; then
    observed=$(python3 - "$report_dir" "$script_dir" "$script_dir/graphs.txt" <<'PY'
import glob
import json
import os
import sys

report_dir, live_dir, graph_list = sys.argv[1:]
with open(graph_list) as source:
    graphs = set(line.strip() for line in source
                 if line.strip() and not line.lstrip().startswith("#"))
status = {}
directories = [report_dir]
if os.path.realpath(report_dir) != os.path.realpath(live_dir):
    directories.append(live_dir)
for directory in directories:
    for path in glob.glob(os.path.join(directory, "*.json")):
        try:
            with open(path) as source:
                report = json.load(source)
        except (OSError, ValueError) as error:
            sys.exit("Cannot read report {}: {}".format(path, error))
        if not isinstance(report, dict) or report.get("representation") != "zuckerli" or report.get("algorithm") != "mvm":
            continue
        input_data = report.get("input", {})
        graph = input_data.get("graph_name") if isinstance(input_data, dict) else None
        if graph not in graphs:
            continue
        parameters = report.get("parameters", {})
        if not isinstance(parameters, dict):
            continue
        # This script submits the default MVM experiment. A custom epsilon or
        # weight file needs its own submission plan and must not fill a slot.
        if (parameters.get("mvm_epsilon") != 0.01 or
                parameters.get("weight_generator") != "mt19937_64_rejection" or
                parameters.get("weight_seed") != 19):
            continue
        execution = report.get("mvm_execution", {})
        seed = parameters.get("mvm_seed")
        current = execution.get("status") if isinstance(execution, dict) else None
        if type(seed) is not int or not 1 <= seed <= 5:
            continue
        if current not in ("complete", "time_limit_partial"):
            sys.exit("Invalid MVM status in {}".format(path))
        key = (graph, seed)
        if current == "complete" or key not in status:
            status[key] = current
for (graph, seed), current in sorted(status.items()):
    print("{}\t{}\t{}".format(graph, seed, current))
PY
)
    if (( !dry_run )); then mkdir -p logs; fi
    total=0
    while IFS= read -r graph || [[ -n "$graph" ]]; do
        graph=${graph%$'\r'}
        [[ -z "$graph" || "$graph" == \#* ]] && continue
        for ((seed=1; seed<=5; ++seed)); do
            current=$(awk -F '\t' -v graph="$graph" -v seed="$seed" '
                $1 == graph && $2 == seed { print $3; found = 1; exit }
                END { if (!found) print "missing" }
            ' <<< "$observed")
            if [[ "$current" == complete ||
                  ( "$current" == time_limit_partial && "$retry_partial" == 0 ) ]]; then
                continue
            fi
            printf 'mvm %s seed %d: %s, submit\n' "$graph" "$seed" "$current"
            total=$((total + 1))
            if (( !dry_run )); then
                sbatch_args=(--job-name="mvm-${graph}-retry-${seed}"
                    --output="logs/mvm-${graph}-retry-${seed}-%j.out"
                    --signal=B:USR1@600 --export="ALL,MVM_SEED=${seed}")
                if [[ -n "$time_limit" ]]; then
                    sbatch_args+=(--time="$time_limit")
                fi
                sbatch "${sbatch_args[@]}" run.sh mvm "$graph"
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

report_counts=$(python3 - "$report_dir" "$script_dir" "$choice" "$script_dir/graphs.txt" <<'PY'
import collections
import glob
import hashlib
import json
import os
import sys

report_dir, live_dir, choice, graph_list = sys.argv[1:]
algorithms = ("matching", "bfs", "dfs", "wvc") if choice == "all" else (choice,)
with open(graph_list) as source:
    graphs = set(line.strip() for line in source
                 if line.strip() and not line.lstrip().startswith("#"))

counts = collections.Counter()
source_algorithms = set()
seen = set()
directories = [report_dir]
if os.path.realpath(report_dir) != os.path.realpath(live_dir):
    directories.append(live_dir)
for directory in directories:
    for path in glob.glob(os.path.join(directory, "*.json")):
        try:
            with open(path) as source:
                report = json.load(source)
        except (OSError, ValueError) as error:
            sys.exit("Cannot read report {}: {}".format(path, error))
        if not isinstance(report, dict) or report.get("representation") != "zuckerli":
            continue
        algorithm = report.get("algorithm")
        input_data = report.get("input")
        graph = input_data.get("graph_name") if isinstance(input_data, dict) else None
        if algorithm not in algorithms or graph not in graphs:
            continue
        if directory == report_dir:
            source_algorithms.add(algorithm)
        job_id = report.get("slurm_job_id")
        if job_id is not None and str(job_id):
            identity = (algorithm, graph, "job", str(job_id))
        else:
            # Local reports lack a job ID. Identical copies still count once.
            with open(path, "rb") as source:
                identity = (algorithm, graph, "contents",
                            hashlib.sha256(source.read()).hexdigest())
        if identity not in seen:
            seen.add(identity)
            counts[algorithm, graph] += 1

missing = set(algorithms) - source_algorithms
if missing:
    sys.exit("No Zuckerli reports for {} in {}".format(
        ", ".join(sorted(missing)), report_dir))
for (algorithm, graph), count in sorted(counts.items()):
    print("{}\t{}\t{}".format(algorithm, graph, count))
PY
)

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
        printf '%s %s: %d/5 reports, submit %d\n' \
            "$algorithm" "$graph" "$existing" "$needed"
        total=$((total + needed))
        for ((run=1; run<=needed; ++run)); do
            if (( !dry_run )); then
                sbatch_args=(--job-name="${algorithm}-${graph}-retry-${run}"
                    --output="logs/${algorithm}-${graph}-retry-${run}-%j.out")
                if [[ -n "$time_limit" ]]; then
                    sbatch_args+=(--time="$time_limit")
                fi
                sbatch "${sbatch_args[@]}" run.sh "$algorithm" "$graph"
            fi
        done
    done < graphs.txt
done
if (( dry_run )); then
    printf 'Would submit %d jobs.\n' "$total"
else
    printf 'Submitted %d jobs.\n' "$total"
fi
