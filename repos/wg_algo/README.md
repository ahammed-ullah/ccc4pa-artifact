# wg_algo

`wg_algo` runs matching, BFS, DFS, weighted vertex cover, and vertex-weighted
matching on the WebGraph files produced by `wg_con`. It is the C++ comparison
implementation for [`cp_algo`](../cp_algo/README.md) in Section 5.3,
Figures 12–13, and Appendix C.3 of
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).
The input is the selected `GRAPH.graph` in `format_wg/GRAPH/`, accompanied by
`.properties` and `.ef`.
Reordered graphs also have `.perm`.

The program keeps the compressed graph in memory. Every neighbor request fills
one reusable buffer with the requested vertex's complete row. The default
bulk decoder rebuilds referenced rows in order, using temporary buffers only
within the current request. The optional iterator decoder advances WebGraph
reference rows one successor at a time. Neither decoder caches rows across
requests. DFS reads each vertex's row once and keeps pending children in
`O(n)` space. All decoding belongs to the measured algorithm time.
The [representation notes](docs/representation.md) explain the decoder's
memory use and its connection to the paper's graph operations.

## Build and run

```bash
make
bin/wg_algo matching /path/to/format_wg/math/math.graph
bin/wg_algo bfs /path/to/format_wg/math/math.graph
bin/wg_algo dfs /path/to/format_wg/math/math.graph
bin/wg_algo wvc /path/to/format_wg/math/math.graph
bin/wg_algo mvm /path/to/format_wg/math/math.graph
```

The CLI is:

```text
wg_algo <matching|bfs|dfs|wvc|mvm> GRAPH.graph [--config CONFIG.toml]
```

Matching defaults to the paper's 3/2 approximation. To run only the initial
maximal matching, use `--config config/maximal.toml`. WVC and MVM use the same
default weights as `cp_algo`. MVM defaults to `mvm_epsilon = 0.01` and sampling
seed `1`; a config may change these or supply a weight file. The program writes one
JSON report in the current directory for every successful run. Repeated and
concurrent runs receive separate report names.

The default decoder is `bulk`. To use the earlier iterator decoder, add
`decoder = "iterator"` to a config file, or use `config/iterator.toml`:

```bash
bin/wg_algo bfs /path/to/format_wg/math/math.graph --config config/iterator.toml
```

Both choices run the same five algorithm implementations on the same encoded
files. The report records the decoder in `parameters.decoder`; both modes use
ordinary report names. Collect each mode's reports in its own directory. See
[representation.md](docs/representation.md) for the decoding steps and memory
tradeoff.

Build with GNU Make and a C++17 compiler that supports unsigned 128-bit
arithmetic. The full `make test` suite uses Python 3, the `wg_encode` binary
from `wg_con`, and the standalone WebGraph CLI. By default it looks for
`../wg_con/target/release/wg_encode` and `../wg_con/bin/webgraph`. Set
`WG_ENCODER` and `WEBGRAPH_BIN` to override those paths. The independent
`make test-mvm` target checks vertex-weighted matching on small graphs.

## Connection to the paper

The algorithms use the same graph as `cp_algo` and report the same kinds of
results.

| Command | Paper step | Reported result |
| --- | --- | --- |
| `matching` | Figures 5 and 6 | Matching size |
| `bfs` | Figure 9, extended to a forest | Number of components |
| `dfs` | Figure 10, extended to a forest | Number of components |
| `wvc` | Figure 18's primal-dual edge update | Sum of selected weights |
| `mvm` | Pettie–Sanders Random-Match | Sum of matched vertex weights |

The cover algorithms visit cliques; these implementations visit graph edges
and neighborhoods. Greedy matching and WVC can therefore choose different
solutions while satisfying the same guarantees. See
[algorithms.md](docs/algorithms.md) for the steps and their memory use.

## Measurements

The algorithm timer starts after `.graph`, `.properties`, and `.ef` have been
loaded and after WVC or MVM weights have been prepared. The report also includes
load time, complete WebGraph bundle size, and process peak RSS, using the
same definitions as `cp_algo`. The paper uses the bulk decoder with no config
file and takes the median of five measurements per graph and algorithm. MVM
uses `ε = 0.01` with sampling seeds 1 through 5.
[Reporting guide](docs/reporting.md) · [Slurm instructions](docs/slurm.md)

## Source map

- `src/graph.cpp`: WebGraph bitstream, `.ef` lookup, and the two full-row
  decoding methods.
- `src/matching.cpp`, `src/bfs.cpp`, `src/dfs.cpp`, `src/wvc.cpp`,
  `src/mvm.cpp`: the five
  algorithm loops.
- `src/main.cpp`: input preparation, timed call, and result.
- `src/config.cpp`, `src/weights.cpp`, `src/report.cpp`: configuration,
  canonical weights, and JSON reports.
