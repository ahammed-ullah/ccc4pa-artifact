#!/bin/bash
set -euo pipefail

if [[ $# -ne 0 ]]; then
    echo "Usage: ./submit.sh" >&2
    exit 2
fi

mkdir -p logs

repetitions=5
line_number=0
while read -r graph delta extra ||
      [[ -n "${graph:-}${delta:-}${extra:-}" ]]; do
    ((line_number += 1))
    delta=${delta%$'\r'}
    [[ -z "${graph:-}" || "$graph" == \#* ]] && continue

    if [[ -z "${delta:-}" || -n "${extra:-}" ]]; then
        echo "Invalid selected graph-delta pair on line ${line_number}" >&2
        exit 2
    fi

    for ((run=1; run<=repetitions; ++run)); do
        sbatch \
            --time=04:00:00 \
            --job-name="fm-${graph}-${run}" \
            --output="logs/fm-${graph}-${run}-%j.out" \
            run.sh "$graph" "$delta"
    done
done < selected_delta.txt
