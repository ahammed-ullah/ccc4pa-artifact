# zk_algo

`zk_algo` runs matching, BFS, DFS, weighted vertex cover, and vertex-weighted
matching on the modified Zuckerli format written by `zk_con`. It supplies the
Zuckerli comparison for [`cp_algo`](../cp_algo/README.md) in Section 5.3,
Figures 12–13, and Appendix C.3 of
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).
The input is `format_zk/GRAPH/GRAPH.zk`, accompanied by `.zki` and, for a
reordered graph, `.perm`. Use `zk_con` to produce these files in the required
format. [Representation notes](docs/representation.md)

The graph stays compressed. Each neighbor request reconstructs one complete
row into a reusable buffer; the next request overwrites it. The decoder keeps
no previously decoded row for later requests. Its reference-chain work and
full-row decoding count in algorithm time. See [representation.md](docs/representation.md).

## Build and run

```bash
make
bin/zk_algo matching /path/to/format_zk/math/math.zk
bin/zk_algo bfs /path/to/format_zk/math/math.zk
bin/zk_algo dfs /path/to/format_zk/math/math.zk
bin/zk_algo wvc /path/to/format_zk/math/math.zk
bin/zk_algo mvm /path/to/format_zk/math/math.zk
```

The CLI is:

```text
zk_algo <matching|bfs|dfs|wvc|mvm> GRAPH.zk [--config CONFIG.toml]
```

Matching defaults to the paper's 3/2 approximation. A config with
`matching_variant = "maximal"` stops after the initial maximal matching.
WVC and MVM use the same seeded or file-supplied canonical vertex weights as
`cp_algo` and `wg_algo`. MVM defaults to `mvm_epsilon = 0.01` and a sampling
seed of `1`; both may be set in the config. The Slurm submit script uses five
different sampling seeds. Every successful invocation writes one JSON report
in the current directory; concurrent runs reserve different names. MVM stops
early if its matching covers every vertex, since that is already optimal.

## Connection to the paper

| Command | Paper step | Reported result |
| --- | --- | --- |
| `matching` | Figures 5 and 6 | Matching size |
| `bfs` | Figure 9, extended to all roots | Number of connected components |
| `dfs` | Figure 10, extended to all roots | Number of connected components |
| `wvc` | Figure 18's primal-dual update, applied to each edge | Sum of selected weights |
| `mvm` | Pettie–Sanders Random-Match, specialized to vertex weights | Sum of matched weights |

Matching, BFS, DFS, and weighted vertex cover use the same steps as `wg_algo`.
The two vertex-weighted matching implementations both choose a move with the
largest positive gain. Their tie rules can choose different edges when two
moves give the same gain.
[Algorithm notes](docs/algorithms.md)

## Verification

Build and run the integration tests with GNU Make, a C++17 compiler with
128-bit integer support, Python 3, and the sibling `zk_con` checkout.

```bash
make -C ../zk_con
make test
```

To verify an existing bundle, compare every decoded row with its source CCF
outside the measured runs.

```bash
make bin/zk_check
bin/zk_check /path/to/format_zk/math/math.zk /path/to/format_ccf/math.ccf
```

## Measurements and source

The algorithm timer starts after `.zk` and `.zki` are loaded and, for WVC or MVM,
after weights are prepared. It includes every row decode. The report also
records load time, complete bundle size, and process peak RSS. The paper uses
the default settings and the median time and peak RSS over five repetitions
per graph and algorithm. Vertex-weighted matching uses `ε = 0.01` and
sampling seeds 1 through 5.
[Reporting guide](docs/reporting.md) · [Slurm instructions](docs/slurm.md)

- `src/graph.cpp`: `.zk` rows, `.zki` offsets, Huffman decoding, and reference
  resolution.
- `src/matching.cpp`, `src/bfs.cpp`, `src/dfs.cpp`, `src/wvc.cpp`, `src/mvm.cpp`: the five
  algorithm loops.
- `src/main.cpp`: input preparation, timed call, and result.
- `src/config.cpp`, `src/weights.cpp`, `src/report.cpp`: configuration,
  canonical weights, and JSON reports.

The [upstream audit](docs/upstream-audit.md) explains why the original
Zuckerli traversal and decoder code are not used directly.
