#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

integration_dir="build/integration"
rm -rf "$integration_dir"
mkdir -p "$integration_dir"

# Keep the public examples runnable. Graph and run specifications are
# independent, so all six constructions are exercised on one graph and LZF is
# then used to exercise every other graph specification.
example_dir="$integration_dir/examples"
mkdir -p "$example_dir" build/example-output

for construction in bcc bpa cpa dpf fpa lzf; do
    (
        cd "$example_dir"
        ../../../bin/cc_con "$construction" \
            --graph ../../../examples/graphs/matrix-market.toml \
            --run "../../../examples/runs/${construction}.toml"
    )
done

for graph in \
    hypergraph-k-uniform \
    hypergraph-poisson \
    hypergraph-lognormal \
    hypergraph-power-law \
    cornell-simplices \
    cornell-dual-simplices \
    cornell-hyperedges \
    stack-exchange-tag-question; do
    (
        cd "$example_dir"
        ../../../bin/cc_con lzf \
            --graph "../../../examples/graphs/${graph}.toml" \
            --run ../../../examples/runs/lzf.toml
    )
done

(
    cd "$example_dir"
    ../../../bin/cc_con bpa \
        --graph ../../../examples/graphs/cornell-hyperedges.toml \
        --run ../../../examples/runs/write-cover.toml
)

for report in "$example_dir"/*.json; do
    python3 -m json.tool "$report" >/dev/null
done

./bin/cc_check cover \
    --cover build/example-output/cornell-hyperedges-example.ccf \
    --graph examples/graphs/cornell-hyperedges.toml

# Exercise every construction through the public CLI on the same input.
for construction in bcc bpa cpa dpf fpa lzf; do
    (
        cd "$integration_dir"
        ../../bin/cc_con "$construction" \
            --graph ../../tests/data/construction_smoke.toml \
            --run "../../config/runs/${construction}.toml"
    )
done

# Repeating a run must preserve the earlier report and select the -2 suffix.
(
    cd "$integration_dir"
    ../../bin/cc_con lzf \
        --graph ../../tests/data/construction_smoke.toml \
        --run ../../config/runs/lzf.toml
)

# Simultaneous repetitions of one construction and graph must reserve distinct
# report names and leave only complete JSON files after all processes finish.
concurrent_dir="$integration_dir/concurrent-reports"
mkdir -p "$concurrent_dir"
pids=()
for run in {1..8}; do
    (
        cd "$concurrent_dir"
        ../../../bin/cc_con lzf \
            --graph ../../../tests/data/construction_smoke.toml \
            --run ../../../config/runs/lzf.toml \
            >"run-${run}.out" 2>"run-${run}.err"
    ) &
    pids+=("$!")
done
for pid in "${pids[@]}"; do
    wait "$pid"
done

report_count=$(find "$concurrent_dir" -maxdepth 1 -type f \
    -name 'lzf-construction-smoke*.json' | wc -l | tr -d ' ')
if [[ "$report_count" != 8 ]]; then
    echo "Concurrent runs produced $report_count reports instead of 8" >&2
    exit 1
fi
for report in "$concurrent_dir"/*.json; do
    python3 -m json.tool "$report" >/dev/null
done
leftover=$(find "$concurrent_dir" -maxdepth 1 \
    \( -name '*.claim' -o -name '*.tmp' \) -print -quit)
if [[ -n "$leftover" ]]; then
    echo "Concurrent reporting left temporary path $leftover" >&2
    exit 1
fi

# Keep one end-to-end path for each non-CGF source family used in experiments.
(
    cd "$integration_dir"
    ../../bin/cc_con bpa \
        --graph ../../config/graphs/hypergraph-k-example.toml \
        --run ../../config/runs/bpa.toml
    ../../bin/cc_con lzf \
        --graph ../../tests/data/cornell_dual_isolates.toml \
        --run ../../config/runs/lzf.toml
    ../../bin/cc_con lzf \
        --graph ../../tests/data/sx_tag_questions.toml \
        --run ../../config/runs/lzf.toml
    ../../bin/cc_con bpa \
        --graph ../../config/graphs/tiny-mm.toml \
        --run ../../tests/data/output_run.toml
)

# Prepare, validate, consume, and cross-check the canonical graph format.
./bin/cg_prepare \
    --graph config/graphs/tiny-mm.toml \
    --output-dir "$integration_dir/prepared"

# cc_bound accepts the CGF file directly. Exercise all three methods and
# both meaningful exact-truss choices without routing through graph TOML.
for method in linear local-mis local-color; do
    (
        cd "$integration_dir"
        ../../bin/cc_bound "$method" --graph prepared/tiny-mm.cgf
    )
done
for method in local-mis local-color; do
    (
        cd "$integration_dir"
        ../../bin/cc_bound "$method" --truss \
            --graph prepared/tiny-mm.cgf
    )
done

if ./bin/cc_bound linear \
    --graph "$integration_dir/prepared/tiny-mm.cgf.toml"; then
    echo "cc_bound accepted a graph format other than CGF" >&2
    exit 1
fi

./bin/cc_check graph \
    --graph "$integration_dir/prepared/tiny-mm.cgf.toml"
./bin/cc_check cover --cover "$integration_dir/tiny-mm.ccf"
./bin/cc_check cover \
    --cover "$integration_dir/tiny-mm.ccf" \
    --graph config/graphs/tiny-mm.toml
./bin/cc_check cover \
    --cover "$integration_dir/tiny-mm.ccf" \
    --graph "$integration_dir/prepared/tiny-mm.cgf.toml"

python3 -c 'import hashlib,json,re,pathlib; p=pathlib.Path("build/integration/prepared"); d=json.load(open(p/"tiny-mm.cgf.json")); d["graph"]["m"] += 1; raw=(json.dumps(d, indent=2)+"\n").encode(); (p/"bad-metadata.cgf.json").write_bytes(raw); t=(p/"tiny-mm.cgf.toml").read_text().replace("tiny-mm.cgf.json", "bad-metadata.cgf.json"); t=re.sub(r"preparation_report_sha256 = \"[0-9a-f]{64}\"", "preparation_report_sha256 = \""+hashlib.sha256(raw).hexdigest()+"\"", t); (p/"bad-metadata.cgf.toml").write_text(t)'
if ./bin/cc_check graph \
    --graph "$integration_dir/prepared/bad-metadata.cgf.toml"; then
    echo "cc_check accepted inconsistent CGF metadata" >&2
    exit 1
fi

# The normal construction path does not hash the preparation report again. The
# explicit checker must still reject the same false checksum.
python3 -c 'import pathlib,re; p=pathlib.Path("build/integration/prepared"); t=(p/"tiny-mm.cgf.toml").read_text(); t=re.sub(r"preparation_report_sha256 = \"[0-9a-f]{64}\"", "preparation_report_sha256 = \""+"0"*64+"\"", t); (p/"unchecked-report-checksum.cgf.toml").write_text(t)'
if ./bin/cc_check graph \
    --graph "$integration_dir/prepared/unchecked-report-checksum.cgf.toml"; then
    echo "cc_check accepted a false preparation-report checksum" >&2
    exit 1
fi

(
    cd "$integration_dir"
    ../../bin/cc_con lzf \
        --graph prepared/unchecked-report-checksum.cgf.toml \
        --run ../../config/runs/lzf.toml
)

for report in \
    bcc-construction-smoke.json \
    bpa-construction-smoke.json \
    cpa-construction-smoke.json \
    dpf-construction-smoke.json \
    fpa-construction-smoke.json \
    lzf-construction-smoke.json \
    lzf-construction-smoke-2.json \
    bpa-hypergraph-k-example.json \
    lzf-cornell-dual-isolates.json \
    lzf-coQuestions-test.json \
    bpa-tiny-mm.json \
    lzf-tiny-mm.json \
    bound-linear-tiny-mm.json \
    bound-local-mis-tiny-mm.json \
    bound-local-mis-truss-tiny-mm.json \
    bound-local-color-tiny-mm.json \
    bound-local-color-truss-tiny-mm.json \
    prepared/tiny-mm.cgf.json; do
    python3 -m json.tool "$integration_dir/$report" >/dev/null
done

python3 -c 'import json; d=json.load(open("build/integration/dpf-construction-smoke.json")); assert d["algorithm"] == {"name": "dpf", "parameters": {}} and d["algorithm_statistics"]["degeneracy"] >= 0 and d["constructed_cover"]["num_cliques"] <= d["graph"]["m"] and d["constructed_cover"]["cover_size"] <= 2*d["graph"]["m"]'
for construction in bpa cpa fpa; do
    python3 -c 'import json,sys; d=json.load(open(f"build/integration/{sys.argv[1]}-construction-smoke.json")); assert d["postprocess"]["assignment_minimal"] and "num_cliques" in d["postprocess"] and "cover_size" in d["postprocess"] and "time_seconds" in d["postprocess"]' "$construction"
done
python3 -c 'import json; d=json.load(open("build/integration/lzf-cornell-dual-isolates.json")); s=d["graph"]["generation_statistics"]; assert d["graph"]["n"] == s["n_dual"] == 3 and d["graph"]["m"] == s["m_dual"] == 3 and s["original_simplex_count"] == 5 and s["dual_isolated_vertices_removed"] == 2'
python3 -c 'import json; d=json.load(open("build/integration/lzf-coQuestions-test.json")); s=d["graph"]["generation_statistics"]; assert d["graph"]["n"] == s["canonical_question_count"] == 3 and d["graph"]["m"] == s["distinct_edges"] == 3 and s["question_count"] == 4 and s["isolated_questions_removed"] == 1'
python3 -c 'import json; d=json.load(open("build/integration/bpa-tiny-mm.json")); v=d["verification"]; assert d["output"]["format"] == "ccf" and not v["explicitly_requested"] and v["performed"] and v["trigger"] == "cover-output" and v["passed"]'
python3 -c 'import json; d=json.load(open("build/integration/lzf-tiny-mm.json")); assert d["graph"]["input_format"] == "cgf" and len(d["graph"]["canonical_fingerprint_sha256"]) == 64'
python3 -c 'import json; d=json.load(open("build/integration/bound-linear-tiny-mm.json")); assert d["algorithm"] == {"method":"linear","exact_truss":False} and set(d["lower_bounds"]) == {"global_mis","global_core_color","combined_best"}'
python3 -c 'import json; d=json.load(open("build/integration/bound-local-color-truss-tiny-mm.json")); b=d["lower_bounds"]; assert d["schema_version"] == 2 and d["algorithm"] == {"method":"local-color","exact_truss":True} and "local_mis" in b and "simplicial_dominating_local_mis" in b and "exact_truss" in b and "truss_filtered_local_color" in b and b["combined_best"] >= max(v for k,v in b.items() if k != "combined_best")'
python3 -c 'import hashlib; b=bytearray(open("build/integration/tiny-mm.ccf", "rb").read()); expected=b[128:160].hex(); b[128:160]=bytes(32); assert hashlib.sha256(b).hexdigest() == expected'
python3 -c 'import hashlib; b=bytearray(open("build/integration/prepared/tiny-mm.cgf", "rb").read()); expected=b[128:160].hex(); b[128:160]=bytes(32); assert hashlib.sha256(b).hexdigest() == expected'

test -s "$integration_dir/tiny-mm.ccf"
test -s "$integration_dir/prepared/tiny-mm.cgf"
test -s "$integration_dir/prepared/tiny-mm.cgf.json"
test -s "$integration_dir/prepared/tiny-mm.cgf.toml"

if ./bin/cc_check cover --cover build/format-tests/corrupted.ccf; then
    echo "cc_check accepted a CCF with a corrupted payload" >&2
    exit 1
fi
if ./bin/cc_check cover --cover build/format-tests/bad-offset.ccf; then
    echo "cc_check accepted a CCF with invalid offsets" >&2
    exit 1
fi
