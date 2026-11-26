#!/bin/bash
#SBATCH -A csit
#SBATCH -p highmem
#SBATCH --nodes=1
#SBATCH --time=24:00:00
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128

set -euo pipefail

graph_dir="${RCAC_SCRATCH}/summer2026/format_cgf"

cd "$SLURM_SUBMIT_DIR"

# bound_large.sh uses this worker so that lower-bound runs receive the same
# high-memory allocation as ordinary construction runs.
if [[ "${1:-}" == "--bound" ]]; then
    method=${2:?Usage: sbatch run_highmem.sh --bound <method> <graph-name> [--truss]}
    graph=${3:?Usage: sbatch run_highmem.sh --bound <method> <graph-name> [--truss]}
    shift 3

    ./bin/cc_bound "$method" \
        --graph "${graph_dir}/${graph}.cgf" \
        "$@"
    exit 0
fi

construction=${1:?Usage: sbatch run_highmem.sh <construction> <graph-name>}
graph=${2:?Usage: sbatch run_highmem.sh <construction> <graph-name>}

./bin/cc_con "$construction" \
    --graph "${graph_dir}/${graph}.cgf.toml" \
    --run "config/runs/${construction}.toml"
