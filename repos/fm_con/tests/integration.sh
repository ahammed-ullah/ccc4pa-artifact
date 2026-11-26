#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/fm-con-test.XXXXXX")
trap 'rm -rf "$work"' EXIT

cd "$work"
"$root/bin/fm_con" --graph "$root/examples/tiny.cgf" >stdout.txt
"$root/bin/fm_con" --graph "$root/examples/tiny.cgf" \
    --workspace prefix-tree >prefix-stdout.txt
"$root/bin/fm_con" --graph "$root/examples/tiny.cgf" \
    --delta 0.5 >fixed-stdout.txt

python3 - "$work/fm-tiny.json" "$work/fm-tiny-2.json" \
    "$work/fm-tiny-delta-0.5.json" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
with path.open(encoding="utf-8") as stream:
    report = json.load(stream)

prefix_path = pathlib.Path(sys.argv[2])
with prefix_path.open(encoding="utf-8") as stream:
    prefix_report = json.load(stream)

fixed_path = pathlib.Path(sys.argv[3])
with fixed_path.open(encoding="utf-8") as stream:
    fixed_report = json.load(stream)

assert report["schema_version"] == 4
assert report["algorithm"]["name"] == "fm"
assert report["algorithm"]["workspace"] == "linear"
assert report["graph"]["n"] == 5
assert report["graph"]["m"] == 4
assert report["delta_search"]["candidate_count"] == 0
assert report["delta_search"]["candidate_delta_min"] is None
assert report["partition"]["initial_cross_edges"] == 8
assert report["partition"]["total_size"] == 16
assert report["fm_representation"]["m_star"] == 13
assert report["fm_representation"]["n_star"] == 10
assert report["construction_state"]["space_bound"] == "O(n+m)"
assert prefix_report["algorithm"]["workspace"] == "prefix-tree"
assert prefix_report["construction_state"]["space_bound"] == "O(n+m log n)"
assert prefix_report["partition"] == report["partition"]
assert fixed_report["algorithm"]["delta_selection"] == "fixed"
assert fixed_report["delta_search"]["policy"] == "fixed-delta"
assert fixed_report["delta_search"]["candidate_count"] == 1
assert fixed_report["delta_search"]["candidate_delta_min"] == 0.5
assert fixed_report["delta_search"]["candidate_delta_max"] == 0.5
assert fixed_report["delta_search"]["selected_delta"] == 0.5
assert fixed_report["delta_search"]["selected_initial_k"] == 0
assert fixed_report["partition"] == report["partition"]
assert "selected_run_seconds" in fixed_report["timing"]
PY

# The fixed-delta timing lists must cover the same 47 regular graphs and one
# high-memory graph as the dataset inventory, with one valid value per graph.
python3 - "$root" <<'PY'
from decimal import Decimal
from pathlib import Path
import sys

root = Path(sys.argv[1])

def graph_names(path):
    return [line.strip() for line in path.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")]

def selected(path):
    result = {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        assert len(fields) == 2, (path, number)
        graph, delta_text = fields
        delta = Decimal(delta_text)
        assert Decimal(0) < delta < Decimal(1), (path, number)
        assert graph not in result, (path, graph)
        result[graph] = delta
    return result

regular = selected(root / "selected_delta.txt")
long = selected(root / "selected_delta_long.txt")
large = selected(root / "selected_delta_large.txt")
assert not (set(regular) & set(long))
assert set(regular) | set(long) == set(graph_names(root / "graphs.txt"))
assert set(large) == set(graph_names(root / "graphs_large.txt"))
assert len(regular) == 36
assert len(long) == 11
assert len(large) == 1
PY

# Simultaneous repetitions use one graph, delta, and working directory. Their
# report claims must still produce distinct complete JSON files.
concurrent="$work/concurrent"
mkdir -p "$concurrent"
pids=()
for run in {1..8}; do
    (
        cd "$concurrent"
        "$root/bin/fm_con" --graph "$root/examples/tiny.cgf" --delta 0.5 \
            >"run-${run}.out" 2>"run-${run}.err"
    ) &
    pids+=("$!")
done
for pid in "${pids[@]}"; do
    wait "$pid"
done

report_count=$(find "$concurrent" -maxdepth 1 -type f \
    -name 'fm-tiny-delta-0.5*.json' | wc -l | tr -d ' ')
if [[ "$report_count" != 8 ]]; then
    echo "Concurrent runs produced $report_count reports instead of 8" >&2
    exit 1
fi
for report in "$concurrent"/*.json; do
    python3 -m json.tool "$report" >/dev/null
done
leftover=$(find "$concurrent" -maxdepth 1 \
    \( -name '*.claim' -o -name '*.tmp' \) -print -quit)
if [[ -n "$leftover" ]]; then
    echo "Concurrent reporting left temporary path $leftover" >&2
    exit 1
fi

if "$root/bin/fm_con" --graph "$root/examples/tiny.cgf" \
    --delta 1 >invalid-stdout.txt 2>invalid-stderr.txt; then
    echo "fm_con accepted delta=1" >&2
    exit 1
fi
grep -q "0 < delta < 1" invalid-stderr.txt

echo "fm_con integration test passed"
