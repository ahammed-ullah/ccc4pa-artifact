# Clique-cover application runs

These runs supply the Plain and VByte results in Figures 12 and 13 and the
solution-quality comparisons in Appendix C.3.
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096)

## Prepare the cover and run an algorithm

Each graph uses the cover written by BPA with assignment-minimal
postprocessing enabled. Use the `BPA+ VByte cover construction` instructions
to prepare that `.ccf` file. The saved cover reports and application reports
record its checksum. The original 48 graph names are in the frozen `cp_algo`
graph list.
[Cover construction](../cc_con/README.md#bpa-vbyte-cover-construction) · [Graph list](../../../repos/cp_algo/graphs.txt)

From the artifact root, set `cover` to the absolute path of the prepared CCF
and choose `matching`, `mvm`, `bfs`, `dfs`, or `wvc`. Run each mode five times
for each graph and algorithm. The default Plain mode decodes the entire cover
before the algorithm is timed. The VByte mode starts the algorithm with the
encoded cover in memory.

```sh
cp_repo="$PWD/repos/cp_algo"
cover=/absolute/path/to/graph.ccf
algorithm=matching
work_dir=$(mktemp -d)
make -C "$cp_repo"

(
  cd "$work_dir"
  for repetition in 1 2 3 4 5; do
    "$cp_repo/bin/cp_algo" "$algorithm" "$cover"
    "$cp_repo/bin/cp_algo" "$algorithm" "$cover" --config "$cp_repo/config/encoded.toml"
  done
)
```

The paper uses the default 3/2-approximate cardinality matching and bounded
vertex-weighted matching with `epsilon = 0.01` and up to 30 rounds. The weighted
algorithms generate the same positive integer weights using seed 19. The
separate encoded vertex-weighted matching batch ran from a checkout named
`cp_algo4` with the same source and effective settings.
[Weight generation](../../../repos/cp_algo/docs/weights.md) · [Original Slurm launchers](../../../repos/cp_algo/docs/slurm.md)

## Read the saved measurements

The artifact has all five reports per graph and algorithm for both cover modes.
The algorithm time includes any lists built during execution. The memory
measurement is the process peak through the end of the timed algorithm and
can include preparation. For a comparison, take the median of each measurement
over the five runs.
[Plain reports](../../../data/evals_app/run-reports/cp_algo/plain/) · [VByte reports](../../../data/evals_app/run-reports/cp_algo/vbyte/) · [Reporting guide](../../../repos/cp_algo/docs/reporting.md)
