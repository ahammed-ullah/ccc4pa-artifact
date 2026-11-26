# WebGraph construction runs

These runs supply the WebGraph sizes and construction times for Figure 23
and the search subtotal in Appendix C.3.
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096)

## Inputs and runs

Use the BPA+ VByte clique cover (`.ccf`) for each of the 48 graphs. From
`repos/wg_con/`, build with `make` and run `./submit.sh` on Negishi. Use
`./submit_resume.sh` to continue searches with timed-out or deadline-skipped
trials. The frozen `Cargo.lock` pins the encoder dependencies, and the
Makefile installs WebGraph CLI 0.4.2 to build the `.ef` index.
[BPA+ CCF-writing instructions](../cc_con/README.md) · [Build and run instructions](../../../repos/wg_con/README.md) · [Graph list](../../../repos/wg_con/graphs.txt)

The search tests windows 16 and 32 in the original and BFS+LLP orders. It
tests windows 64 and 128 in the order with the smaller completed window-32
bundle. All 48 winners use window 128. Of these, 44 use BFS+LLP and four use
the original order. The selected `.graph`, `.properties`, and `.ef` files form
the runtime input for `wg_algo`, together with `.perm` for reordered graphs.
`zk_con` reuses the selected order and its `.perm` when present.
[WebGraph application runs](../wg_algo/README.md) · [Zuckerli construction runs](../zk_con/README.md)

## Saved measurements

- `webgraph_bytes` counts the graph, properties, index, and permutation
  when used. [Selected sizes and settings](../../../data/evals_con/run-reports/wg_con/selected-encoding-sizes.csv)
- `selected_attempt_seconds` adds the selected encoding and index times for
  Figure 23. The selected trials used single-threaded encoding and indexing
  with the vertex order already available. [Selected construction times](../../../data/evals_con/run-reports/wg_con/selected-encoding-times.csv)
- `successful_attempt_seconds` sums the same phases for the six successful
  candidates per graph. It includes any multithreaded BFS+LLP ordering done
  within those trials. This is a lower bound on full per-graph search time
  because it omits interrupted trials and work outside the timed subprocesses.
  [Successful-search totals](../../../data/evals_con/run-reports/wg_con/successful-encoding-search-times.csv)

The saved workbooks combine the WebGraph, BPA+, and Zuckerli measurements.
[Size workbook](../../../data/evals_con/summaries/graph-representation-sizes.xlsx) · [Construction-time workbook](../../../data/evals_con/summaries/graph-representation-construction-times.xlsx)
