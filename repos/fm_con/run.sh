#!/bin/bash
#SBATCH -A csit
#SBATCH -p cpu
#SBATCH --nodes=1
#SBATCH --time=24:00:00
#SBATCH --mem-per-cpu=128GB

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: sbatch run.sh <graph-name> [delta]" >&2
    exit 2
fi

graph=$1
graph_dir="${RCAC_SCRATCH}/summer2026/format_cgf"

cd "$SLURM_SUBMIT_DIR"

command=(./bin/fm_con --graph "${graph_dir}/${graph}.cgf")
if [[ $# -eq 2 ]]; then
    command+=(--delta "$2")
fi
"${command[@]}"
