#!/bin/bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$script_dir"

count=0
while IFS= read -r graph || [[ -n "$graph" ]]; do
    graph=${graph%$'\r'}
    [[ -z "$graph" || "$graph" == \#* ]] && continue
    count=$((count + 1))
done < graphs.txt
[[ $count -gt 0 ]] || { echo "graphs.txt has no graphs" >&2; exit 1; }

mkdir -p logs
# The array range is the only submission choice here; run.sh owns the job
# settings. Extra sbatch options can still override its defaults.
sbatch --array="0-$((count - 1))" "$@" run.sh
