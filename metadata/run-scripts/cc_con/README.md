# Clique-cover construction experiments

The saved `cc_con` reports support Figure 11, Table 4, Figures 21–22, and the
VByte cover measurements in Figure 23.
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096)

## Graph preparation and environment

Use the graph specifications and run configurations in the frozen snapshot.
Each source TOML identifies the graph through `graph.name`, which determines
the filename of the prepared graph and its report. For file-backed inputs,
place the files under `repos/cc_con/datasets/` at the relative paths given in
each TOML. Synthetic specifications record their
generator settings. The dataset metadata records the canonical graph sizes and
fingerprints.
[Graph specifications](../../../repos/cc_con/config/graphs/) · [Dataset metadata](../../../data/datasets/)

The paper reports single-threaded runs compiled with GCC 12.2.0 and `-O3` on
AMD Milan processors at 2.2 GHz with 256–1024 GB of memory. Select the compiler
through `CXX` if needed. The snapshot Makefile defaults to `-O3 -DNDEBUG`.

For one graph, start at the artifact root and choose the source specification
and its graph name. The commands keep prepared inputs, covers, and reports in
a temporary work directory.

```sh
artifact_root="$PWD"
cc_con_root="$artifact_root/repos/cc_con"
work_dir=$(mktemp -d)
graph_name=coPapersCiteseer
graph_spec="$cc_con_root/config/graphs/$graph_name.toml"

make -C "$cc_con_root"
mkdir -p "$work_dir/prepared" "$work_dir/construction-reports"
"$cc_con_root/bin/cg_prepare" --graph "$graph_spec" --output-dir "$work_dir/prepared"
prepared_graph="$work_dir/prepared/$graph_name.cgf.toml"
```

## Construction statistics

Run each construction five times per graph. The tracked specifications use
`tau = 4.0` and `label_selection = "min-label"` for BPA, FPA, and CPA and
enable assignment-minimal postprocessing for these three methods. BCC, LZF,
and DPF use the constructed cover. The statistics runs write JSON reports.
[Run specifications](../../../repos/cc_con/config/runs/) · [Saved construction reports](../../../data/evals_con/run-reports/cc_con/construction/)

```sh
(
  cd "$work_dir/construction-reports"
  for algorithm in lzf bcc fpa cpa bpa dpf; do
    for repetition in 1 2 3 4 5; do
      "$cc_con_root/bin/cc_con" "$algorithm" \
        --graph "$prepared_graph" \
        --run "$cc_con_root/config/runs/$algorithm.toml"
    done
  done
)
```

For the construction-only timings, take the median of
`constructed_cover.time_seconds` across the five reports. It measures the
construction call and excludes graph preparation, postprocessing,
verification, encoding, and report writing. Use
`constructed_cover.cover_size` for the raw cover. For the assignment-minimal
variants BPA+, FPA+, and CPA+, use `postprocess.cover_size` and take the median
of the per-run sums of `constructed_cover.time_seconds` and
`postprocess.time_seconds`. DPF is assignment-minimal by construction and uses
the raw fields.
[Report field definitions](../../../repos/cc_con/docs/reporting.md)

## Assignment-optimal lower bounds

`cc_bound` takes the prepared `.cgf` directly. The saved suite contains 48
`linear`, 29 `local-mis`, 17 `local-mis --truss`, and 8 `local-color`
reports. Use the method and `exact_truss` setting from the corresponding
saved report to select a command. The paper's approximation-ratio denominator
uses the maximum available `lower_bounds.combined_best` for each graph.
[Saved lower-bound reports](../../../data/evals_con/run-reports/cc_con/lower-bounds/) · [Lower-bound CLI](../../../repos/cc_con/docs/cc-bound.md)

```sh
mkdir -p "$work_dir/lower-bound-reports"
(
  cd "$work_dir/lower-bound-reports"
  "$cc_con_root/bin/cc_bound" linear --graph "$work_dir/prepared/$graph_name.cgf"
)
```

## BPA+ VByte cover construction

`bpa-vbyte-cover.toml` adds CCF output to the tracked BPA settings and matches
the construction, postprocessing, and output settings visible in the 48 saved
reports. The output directory is relative to the run specification. Copy the
specification into the work directory and create its `covers/` directory
before running.
[Artifact run specification](bpa-vbyte-cover.toml) · [Saved BPA+ VByte reports](../../../data/evals_con/run-reports/cc_con/bpa-vbyte-cover-construction/)

```sh
mkdir -p "$work_dir/covers" "$work_dir/vbyte-reports"
cp "$artifact_root/metadata/run-scripts/cc_con/bpa-vbyte-cover.toml" "$work_dir/"
(
  cd "$work_dir/vbyte-reports"
  "$cc_con_root/bin/cc_con" bpa --graph "$prepared_graph" \
    --run "$work_dir/bpa-vbyte-cover.toml"
)
```

The command writes `covers/<graph>.ccf` under the work directory. Output
implies semantic verification before encoding. Figure 23 uses
`output.encoded_bytes` for size and the sum of
`constructed_cover.time_seconds`, `postprocess.time_seconds`, and
`output.encoding_and_write_time_seconds` for the BPA+ running time.
Verification time is recorded separately.

## Tracked Slurm launchers

The snapshot keeps the original working-directory layout. Submit from the
snapshot root after building the binaries there. `cg_prepare.sh` takes the
source TOML's filename stem and writes prepared inputs below
`${RCAC_SCRATCH}/summer2026/format_cgf`. The job scripts change into
`SLURM_SUBMIT_DIR`. The regular launchers use `graphs.txt` for 47 graphs and
the high-memory launchers use `graphs_large.txt` for the remaining graph.
Adjust the Slurm account, allocation, and local scratch paths in a working copy
when using a different cluster.

| Stage | Batch entry point | Worker |
| --- | --- | --- |
| Prepare graph | `sbatch cg_prepare.sh <specification-stem>` | [`cg_prepare.sh`](../../../repos/cc_con/cg_prepare.sh) |
| Five construction repetitions | [`submit.sh`](../../../repos/cc_con/submit.sh) | [`run.sh`](../../../repos/cc_con/run.sh) |
| Five high-memory construction repetitions | [`submit_highmem.sh`](../../../repos/cc_con/submit_highmem.sh) | [`run_highmem.sh`](../../../repos/cc_con/run_highmem.sh) |
| Lower bound | [`bound.sh`](../../../repos/cc_con/bound.sh) | [`run.sh`](../../../repos/cc_con/run.sh) |
| High-memory lower bound | [`bound_large.sh`](../../../repos/cc_con/bound_large.sh) | [`run_highmem.sh`](../../../repos/cc_con/run_highmem.sh) |
