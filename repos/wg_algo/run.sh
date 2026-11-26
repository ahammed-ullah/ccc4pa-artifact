#!/bin/bash
#SBATCH -A csit
#SBATCH -p cpu
#SBATCH --nodes=1
#SBATCH --cpus-per-task=1
#SBATCH --time=4:00:00
#SBATCH --mem=128G

set -euo pipefail

algorithm=${1:?Usage: sbatch run.sh <algorithm> <graph-name> [config.toml]}
graph=${2:?Usage: sbatch run.sh <algorithm> <graph-name> [config.toml]}
graph_directory="${RCAC_SCRATCH:?Set RCAC_SCRATCH}/fall2026/format_wg/${graph}"

cd "${SLURM_SUBMIT_DIR:?Submit from wg_algo}"

if [[ $# -eq 3 ]]; then
    exec ./bin/wg_algo "$algorithm" "${graph_directory}/${graph}.graph" --config "$3"
else
    exec ./bin/wg_algo "$algorithm" "${graph_directory}/${graph}.graph"
fi
