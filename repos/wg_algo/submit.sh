#!/bin/bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$script_dir"

usage='Usage: submit.sh [--time HH:MM:SS] <matching|bfs|dfs|wvc|mvm> [config.toml] [graph_list.txt]'
time_override=
positionals=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --time)
            [[ $# -ge 2 ]] || { echo "$usage" >&2; exit 2; }
            time_override=$2
            shift 2 ;;
        -*) echo "$usage" >&2; exit 2 ;;
        *) positionals+=("$1"); shift ;;
    esac
done
if (( ${#positionals[@]} < 1 || ${#positionals[@]} > 3 )); then
    echo "$usage" >&2
    exit 2
fi
algorithm=${positionals[0]}
case "$algorithm" in matching|bfs|dfs|wvc|mvm) ;; *) echo "$usage" >&2; exit 2 ;; esac
config=${positionals[1]:-}
graph_list=${positionals[2]:-graphs.txt}
repetitions=5
mkdir -p logs

while IFS= read -r graph || [[ -n "$graph" ]]; do
    graph=${graph%$'\r'}
    [[ -z "$graph" || "$graph" == \#* ]] && continue
    for ((run=1; run<=repetitions; ++run)); do
        options=(--job-name="${algorithm}-${graph}-${run}"
                 --output="logs/${algorithm}-${graph}-${run}-%j.out")
        if [[ -n "$time_override" ]]; then options+=(--time="$time_override"); fi
        if [[ "$algorithm" == mvm ]]; then
            options+=(--signal=B:USR1@600 --export="ALL,MVM_SEED=$run")
        fi
        if [[ -n "$config" ]]; then
            sbatch "${options[@]}" run.sh "$algorithm" "$graph" "$config"
        else
            sbatch "${options[@]}" run.sh "$algorithm" "$graph"
        fi
    done
done < "$graph_list"
