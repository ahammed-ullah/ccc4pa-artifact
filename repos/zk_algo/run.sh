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
graph_directory="${RCAC_SCRATCH:?Set RCAC_SCRATCH}/fall2026/format_zk/${graph}"

cd "${SLURM_SUBMIT_DIR:?Submit from zk_algo}"

if [[ $# -eq 3 ]]; then
    exec ./bin/zk_algo "$algorithm" "${graph_directory}/${graph}.zk" --config "$3"
else
    exec ./bin/zk_algo "$algorithm" "${graph_directory}/${graph}.zk"
fi
