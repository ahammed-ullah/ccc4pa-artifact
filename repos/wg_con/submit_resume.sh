#!/bin/bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$script_dir"
if ! python3 tune.py --help | grep -q -- '--resume-incomplete'; then
    echo "submit_resume.sh: update tune.py before resuming" >&2
    exit 1
fi

scratch=${RCAC_SCRATCH:?Set RCAC_SCRATCH}
output_directory=${WG_OUTPUT_DIR:-"$scratch/fall2026/format_wg"}

# A timeout still leaves a valid winner and manifest. Submit only graphs whose
# manifests record a timed-out trial or a trial skipped at the job deadline.
array_spec=$(python3 - "$output_directory" graphs.txt <<'PY'
import json
from pathlib import Path
import sys

output = Path(sys.argv[1])
graph_list = Path(sys.argv[2])
graphs = [line.strip() for line in graph_list.read_text(encoding="utf-8").splitlines()
          if line.strip() and not line.lstrip().startswith("#")]
indices = []
for index, graph in enumerate(graphs):
    manifest = output / graph / "manifest.json"
    if not manifest.is_file():
        continue
    with manifest.open("r", encoding="utf-8") as source:
        report = json.load(source)
    if any(item.get("status") in ("timeout", "skipped_deadline")
           for item in report.get("attempts", [])):
        indices.append(index)
if indices:
    print("Continuing {} graphs: {}".format(
        len(indices), ", ".join(graphs[index] for index in indices)), file=sys.stderr)
print(",".join(map(str, indices)))
PY
)

if [[ -z "$array_spec" ]]; then
    echo "No timed-out searches found in $output_directory"
    exit 0
fi
if [[ -n ${WG_MAX_PARALLEL:-} ]]; then
    [[ $WG_MAX_PARALLEL =~ ^[1-9][0-9]*$ ]] || {
        echo "WG_MAX_PARALLEL must be positive" >&2
        exit 1
    }
    array_spec="${array_spec}%${WG_MAX_PARALLEL}"
fi
mkdir -p logs
echo "Resuming graph indices: $array_spec"
# The #SBATCH directives in run.sh set account, partition, CPUs, memory, and time.
# A QoS directive, if present there, also takes effect.
# Clear inherited sbatch settings that would override those directives.
unset SBATCH_ACCOUNT SBATCH_PARTITION SBATCH_QOS SBATCH_TIMELIMIT
unset SBATCH_CPUS_PER_TASK SBATCH_MEM_PER_NODE SBATCH_MEM_PER_CPU SBATCH_NODES
sbatch --job-name=wg-resume \
    --array="$array_spec" \
    --output='logs/wg-resume-%A_%a.out' \
    --export=ALL \
    run.sh --resume
