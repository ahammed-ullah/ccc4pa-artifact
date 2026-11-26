#!/bin/bash
#SBATCH -A csit
#SBATCH -p cpu
#SBATCH --job-name=zk-con
#SBATCH --output=logs/zk-con-%A_%a.out
#SBATCH --nodes=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=64G
#SBATCH --time=2-00:00:00

set -euo pipefail

# submit.sh calls sbatch from zk_con. This also works when run.sh is invoked
# directly outside Slurm.
if [[ -n ${SLURM_SUBMIT_DIR:-} ]]; then
    cd "$SLURM_SUBMIT_DIR"
else
    cd "$(dirname -- "${BASH_SOURCE[0]}")"
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
exec python3 run.py \
    --graph "$graph" \
    --ccf-dir "$scratch/fall2026/format_ccf" \
    --wg-dir "$scratch/fall2026/format_wg" \
    --output-dir "$scratch/fall2026/format_zk" \
    --encoder ./bin/zk_con
