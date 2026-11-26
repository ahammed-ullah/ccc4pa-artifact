#!/bin/bash
set -euo pipefail

if [[ $# -ne 0 ]]; then
    echo "Usage: ./submit_delta.sh" >&2
    exit 2
fi

mkdir -p logs

line_number=0
while read -r graph delta extra ||
      [[ -n "${graph:-}${delta:-}${extra:-}" ]]; do
    ((line_number += 1))
    delta=${delta%$'\r'}
    [[ -z "${graph:-}" || "$graph" == \#* ]] && continue

    if [[ -z "${delta:-}" || -n "${extra:-}" ]]; then
        echo "Invalid graph-delta pair on line ${line_number}" >&2
        exit 2
    fi

    sbatch \
        --job-name="fm-${graph}-d${delta}" \
        --output="logs/fm-${graph}-d${delta}-%j.out" \
        run.sh "$graph" "$delta"
done < graph_delta.txt
