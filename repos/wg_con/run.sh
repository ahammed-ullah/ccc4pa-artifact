#!/bin/bash
#SBATCH -A csit
#SBATCH -p cpu
#SBATCH --nodes=1
#SBATCH --cpus-per-task=8
#SBATCH --time=2-00:00:00
#SBATCH --mem=64G

set -euo pipefail

# Slurm runs a copy of this script; use the directory where sbatch was called.
cd "${SLURM_SUBMIT_DIR:-$(dirname -- "${BASH_SOURCE[0]}")}"
[[ -f tune.py ]] || { echo "run.sh: tune.py not found in $PWD; submit from wg_con" >&2; exit 1; }

resume=0
if [[ ${1:-} == --resume ]]; then
    resume=1
    shift
fi
graph=${1:-}
if [[ -z "$graph" ]]; then
    index=${SLURM_ARRAY_TASK_ID:?Pass a graph name or submit as a Slurm array}
    graphs=()
    while IFS= read -r name || [[ -n "$name" ]]; do
        name=${name%$'\r'}
        [[ -z "$name" || "$name" == \#* ]] && continue
        graphs+=("$name")
    done < graphs.txt
    graph=${graphs[index]:?Array index does not name a graph}
fi

scratch=${RCAC_SCRATCH:?Set RCAC_SCRATCH}
cover_directory="$scratch/fall2026/format_ccf"
output_directory=${WG_OUTPUT_DIR:-"$scratch/fall2026/format_wg"}
export RAYON_NUM_THREADS=${SLURM_CPUS_PER_TASK:-8}

resume_args=()
if [[ $resume == 1 || ${WG_RESUME_INCOMPLETE:-0} == 1 ]]; then
    resume_args+=(--resume-incomplete)
    if [[ -n ${WG_RESUME_CANDIDATE_TIMEOUT_SECONDS:-} ]]; then
        resume_args+=(--candidate-timeout-seconds "$WG_RESUME_CANDIDATE_TIMEOUT_SECONDS")
    fi
    echo "run.sh: resuming $graph with --resume-incomplete" >&2
fi

exec python3 tune.py \
    --ccf "$cover_directory/$graph.ccf" \
    --output-dir "$output_directory" \
    --encoder "${WG_ENCODER:-./target/release/wg_encode}" \
    --webgraph "${WEBGRAPH_BIN:-./bin/webgraph}" \
    "${resume_args[@]}"
