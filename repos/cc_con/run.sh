#!/bin/bash
#SBATCH -A csit
#SBATCH -p cpu
#SBATCH --nodes=1
#SBATCH --time=4:00:00
#SBATCH --mem-per-cpu=128GB

set -euo pipefail

graph_dir="${RCAC_SCRATCH}/summer2026/format_cgf"

cd "$SLURM_SUBMIT_DIR"

# bound.sh uses this worker so that lower-bound runs receive the same Slurm
# resources as ordinary construction runs. Any remaining arguments are passed
# directly to cc_bound; currently the only such option is --truss.
if [[ "${1:-}" == "--bound" ]]; then
    method=${2:?Usage: sbatch run.sh --bound <method> <graph-name> [--truss]}
    graph=${3:?Usage: sbatch run.sh --bound <method> <graph-name> [--truss]}
    shift 3

    ./bin/cc_bound "$method" \
        --graph "${graph_dir}/${graph}.cgf" \
        "$@"
    exit 0
fi

construction=${1:?Usage: sbatch run.sh <construction> <graph-name>}
graph=${2:?Usage: sbatch run.sh <construction> <graph-name>}

./bin/cc_con "$construction" \
    --graph "${graph_dir}/${graph}.cgf.toml" \
    --run "config/runs/${construction}.toml"
