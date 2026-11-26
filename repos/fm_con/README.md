# fm_con

`fm_con` is a small evaluation implementation of the Feder-Motwani (FM)
biclique-partition construction from:

Tomas Feder and Rajeev Motwani, "Clique Partitions, Graph Compression and
Speeding-Up Algorithms," *Journal of Computer and System Sciences* 51 (1995),
261-272.

The program reads one canonical simple undirected graph in CGF version 1,
applies the FM construction to its directed bipartite expansion, and writes a
JSON report. It does not write the bicliques or the compressed graph. The
reported values are intended for comparison with the total sizes of clique
covers.

This project has its own CGF decoder. It does not include, link to, or invoke
`cc_con`.

This is the Feder–Motwani baseline in Section 5.2 and Figures 11 and 21.
Appendix D.2 describes the method. The comparison uses `2m / partition.total_size`
for compression and the median of five `timing.selected_run_seconds` values
for each graph. The timed runs use the selected fixed delta and the default
linear workspace. The construction time used in the paper excludes building
the packed input index and selecting delta.
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096)

## Build

Build with a C++17 compiler and GNU Make.

```sh
make
make test
```

The release executable is `bin/fm_con`. The executable itself has no external
library dependency. `make test` uses Bash and Python 3 to run the integration
checks and inspect their JSON reports.

## Run

```sh
./bin/fm_con --graph /absolute/path/to/graph.cgf
```

Without `--delta`, automatic selection may perform `O(log n)` independent
candidate runs, each starting from the original graph. It uses one delta for
each attainable positive initial value of `k_hat` and retains the run with the
smallest total biclique-partition size. [Algorithm and implementation](docs/algorithm.md)
defines the delta values precisely.

To perform a fixed-delta run instead:

```sh
./bin/fm_con --graph /absolute/path/to/graph.cgf --delta 0.6
```

The fixed value must satisfy `0 < delta < 1`.

This uses the default `O(n+m)` workspace, which is `O(m)` under the required
no-isolated-vertices input contract. The original `O(m log n)` sparse
prefix-tree implementation remains available explicitly:

```sh
./bin/fm_con --graph /absolute/path/to/graph.cgf \
    --workspace prefix-tree
```

The other accepted value is `--workspace linear`, which is already the
default. A run uses the selected method for every `k_hat >= 2` search it
performs; it never switches that workspace according to the graph.
Both methods use the same exact specialization after `k_hat` reaches one.

An automatic report is written in the current directory as `fm-graph.json`.
A fixed-delta run preserves the argument in its name, for example
`fm-graph-delta-0.6.json`. If a name already exists, the program adds `-2`,
then `-3`, and so on. Each name is reserved before construction, so simultaneous
repetitions with the same graph and delta safely receive distinct reports.
Standard output contains only a short completion message.

## Evaluation Scripts

The root scripts use the CGF files under the cluster path stated in `run.sh` and
`run_highmem.sh`. They separate delta selection from repeated timing:

- `selected_delta.txt` lists the selected delta for 36 graphs assigned a
  four-hour limit. `selected_delta_long.txt` lists 11 graphs assigned a
  24-hour limit. This includes `dba`, whose recorded 3.68-hour time leaves too
  little margin for a four-hour job. `selected_delta_large.txt` lists the value
  for the high-memory `math` graph.
- `submit.sh`, `submit_long.sh`, and `submit_highmem.sh` schedule five
  independent runs for their respective lists. They always pass `--delta`, so
  these timing runs never repeat automatic selection.
- `graph_delta.txt` and `graph_delta_large.txt` retain the manually submitted
  trial values for the graphs whose automatic search was too expensive to run
  sequentially. `submit_delta.sh` and `submit_delta_highmem.sh` reproduce one
  run for each of those trial values.

For 38 graphs, the selected value is the value retained by a completed
automatic run. For the other 10 graphs, it is the completed fixed trial with
the smallest reported `partition.total_size`. The executable's automatic mode
remains available independently of these evaluation scripts. To repeat one
measurement outside the cluster, pass the corresponding listed value to
`--delta` and use the prepared CGF for that graph.

## Input Contract

The only accepted input is CGF version 1 with delta-vbyte encoded, sorted
adjacency lists. The graph is assumed to be canonical: simple, undirected,
without isolated vertices, and with contiguous vertex IDs in `[0,n)`. Prepare
the CGF with `cc_con`'s `cg_prepare`. Before measuring FM construction, run
`cc_check graph --graph` with the generated `.cgf.toml`. Pass the corresponding
`.cgf` file to `fm_con`.

The decoder checks the header and the boundaries and counts of records it
reads. Runs starting with `k_hat=0` use only the header. Runs starting with
`k_hat=1` read the row sizes and skip the neighbor IDs. Use `cc_check` for full
graph validation. The report copies fingerprints from the CGF header.

Vertex IDs and one neighborhood size must fit in `int`/`uint32_t`. Partition
counts, orders, and edge counts use `uint64_t`; they are not limited by
`INT_MAX`.

## Documentation

- [Algorithm and implementation](docs/algorithm.md)
- [JSON report](docs/reporting.md)
- [Project structure](docs/architecture.md)
- [Small example](examples/README.md)
