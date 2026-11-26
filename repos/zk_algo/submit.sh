#!/bin/bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$script_dir"

usage='Usage: submit.sh [--time D-HH:MM:SS|HH:MM:SS] <matching|bfs|dfs|wvc|mvm> [config.toml] [graph_list.txt]'
time_limit=
if [[ ${1:-} == --time ]]; then
    [[ $# -ge 3 ]] || { echo "$usage" >&2; exit 2; }
    time_limit=$2
    shift 2
fi
if [[ -n "$time_limit" && ! "$time_limit" =~ ^([0-9]+-)?[0-9]+:[0-9][0-9]:[0-9][0-9]$ ]]; then
    echo "Invalid Slurm time: $time_limit" >&2
    exit 2
fi
[[ $# -ge 1 && $# -le 3 ]] || { echo "$usage" >&2; exit 2; }
algorithm=$1
case "$algorithm" in
    matching|bfs|dfs|wvc|mvm) ;;
    *) echo "$usage" >&2; exit 2 ;;
esac
config=${2:-}
graph_list=${3:-graphs.txt}
repetitions=5
mkdir -p logs

while IFS= read -r graph || [[ -n "$graph" ]]; do
    graph=${graph%$'\r'}
    [[ -z "$graph" || "$graph" == \#* ]] && continue
    for ((run=1; run<=repetitions; ++run)); do
        sbatch_args=(--job-name="${algorithm}-${graph}-${run}"
            --output="logs/${algorithm}-${graph}-${run}-%j.out")
        if [[ -n "$time_limit" ]]; then sbatch_args+=(--time="$time_limit"); fi
        if [[ "$algorithm" == mvm ]]; then
            sbatch_args+=(--signal=B:USR1@600 --export="ALL,MVM_SEED=${run}")
        fi
        if [[ -n "$config" ]]; then
            sbatch "${sbatch_args[@]}" run.sh "$algorithm" "$graph" "$config"
        else
            sbatch "${sbatch_args[@]}" run.sh "$algorithm" "$graph"
        fi
    done
done < "$graph_list"
