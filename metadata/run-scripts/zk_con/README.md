# Zuckerli construction runs

These runs supply the Zuckerli bundle sizes and construction times for
Figure 23 and Appendix C.3 of
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).

## Inputs and runs

Use the BPA+ VByte `.ccf` for each graph and the selected WebGraph manifest
generated from that same cover. For a graph with BFS+LLP order, place the
selected WebGraph `.perm` beside the WebGraph manifest. The paper used BFS+LLP
for 44 graphs and the original order for four.
[BPA+ CCF-writing instructions](../cc_con/README.md) · [WebGraph construction guide](../wg_con/README.md) · [Selected WebGraph orders](../../../data/evals_con/run-reports/wg_con/selected-encoding-sizes.csv)

From `repos/zk_con/`, build with `make` and use `bash submit.sh` for the 48
Negishi array tasks. The wrapper checks the selected inputs and places the
`.zk`, `.zki`, optional `.perm`, and `manifest.json` in the output directory
for each graph. The encoder uses a window of 128, unlimited reference chains,
and two rounds of reference selection. It writes the rows with Huffman coding.
[Build and run instructions](../../../repos/zk_con/README.md) · [Fixed settings](../../../repos/zk_con/docs/paper-mapping.md) · [Graph list](../../../repos/zk_con/graphs.txt)

## Saved measurements

The CSV contains one completed encoding for each of the 48 graphs.
`bundle_bytes` is the combined size of `.zk`, `.zki`, and `.perm` when used.
`encode_seconds` measures the encoder subprocess, including CCF loading, both
reference-selection rounds, and writing the compressed graph and its index.
The wrapper checks inputs and copies the selected permutation before starting
the timer. The saved WebGraph order is already available at that point.
Use the CSV timing for Figure 23. The encoder also prints its own elapsed
time in the Slurm log.
[Encoding records](../../../data/evals_con/run-reports/zk_con/encoding-times-and-sizes.csv) · [Size workbook](../../../data/evals_con/summaries/graph-representation-sizes.xlsx) · [Construction-time workbook](../../../data/evals_con/summaries/graph-representation-construction-times.xlsx)

The manifest's experiment revision `0a4932f` identifies the encoder source
and Slurm scripts. Those files are byte-identical in the snapshot. The
snapshot's wrapper matches the current WebGraph manifests by reading the
checksum stored in the CCF header. The earlier wrapper computed a checksum
over the complete CCF file. Both checks run before the encoder timer.
[Repository revisions](../../../repo-versions.tsv)
