#!/bin/bash
set -euo pipefail

method=${1:?Usage: bound.sh <linear|local-mis|local-color> [--truss]}
shift

case "$method" in
    linear|local-mis|local-color) ;;
    *)
        echo "Usage: bound.sh <linear|local-mis|local-color> [--truss]" >&2
        exit 2
        ;;
esac

truss_suffix=""
for option in "$@"; do
    if [[ "$option" != "--truss" ]]; then
        echo "bound.sh: unknown run option '$option'" >&2
        exit 2
    fi
    truss_suffix="-truss"
done

if [[ "$method" == "linear" && -n "$truss_suffix" ]]; then
    echo "bound.sh: --truss is not available with linear" >&2
    exit 2
fi

mkdir -p logs

while IFS= read -r graph || [[ -n "$graph" ]]; do
    graph=${graph%$'\r'}
    [[ -z "$graph" || "$graph" == \#* ]] && continue

    job="bound-${method}${truss_suffix}-${graph}"
    sbatch \
        --job-name="$job" \
        --output="logs/${job}-%j.out" \
        run.sh --bound "$method" "$graph" "$@"
done < graphs.txt
