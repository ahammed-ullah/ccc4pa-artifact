#!/bin/bash
set -euo pipefail

construction=${1:?Usage: submit_highmem.sh <construction>}
repetitions=5
mkdir -p logs

while IFS= read -r graph || [[ -n "$graph" ]]; do
    # read returns failure at EOF when the final line has no newline, even
    # though it still placed that graph name in the variable. The condition
    # above processes that final value. Removing a trailing carriage return
    # also permits graph lists saved with Windows line endings.
    graph=${graph%$'\r'}
    [[ -z "$graph" || "$graph" == \#* ]] && continue

    for ((run=1; run<=repetitions; ++run)); do
        sbatch \
            --job-name="${construction}-${graph}-${run}" \
            --output="logs/${construction}-${graph}-${run}-%j.out" \
            run_highmem.sh "$construction" "$graph"
    done
done < graphs_large.txt
