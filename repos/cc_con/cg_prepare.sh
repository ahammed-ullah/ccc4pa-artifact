#!/bin/bash
#SBATCH -A csit
#SBATCH -p cpu
#SBATCH --nodes=1
#SBATCH --time=4:00:00
#SBATCH --mem-per-cpu=16GB

set -euo pipefail

graph=${1:?Usage: sbatch run_cgf.sh <graph-name>}
output_dir="${RCAC_SCRATCH}/summer2026/format_cgf"

cd "$SLURM_SUBMIT_DIR"

./bin/cg_prepare \
    --graph "config/graphs/${graph}.toml" \
    --output-dir "$output_dir"