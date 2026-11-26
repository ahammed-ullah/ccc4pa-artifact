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
array_spec="0-$((count - 1))"
if [[ -n ${WG_MAX_PARALLEL:-} ]]; then
    [[ $WG_MAX_PARALLEL =~ ^[1-9][0-9]*$ ]] || {
        echo "WG_MAX_PARALLEL must be positive" >&2
        exit 1
    }
    array_spec="${array_spec}%${WG_MAX_PARALLEL}"
fi
cpus=${WG_CPUS:-8}
[[ $cpus =~ ^[1-9][0-9]*$ ]] || { echo "WG_CPUS must be positive" >&2; exit 1; }
memory=${WG_MEMORY:-64G}

mkdir -p logs
sbatch --job-name=wg-size \
    --array="$array_spec" \
    --cpus-per-task="$cpus" --mem="$memory" \
    --output='logs/wg-size-%A_%a.out' \
    run.sh
