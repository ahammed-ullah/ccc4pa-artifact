# Zuckerli construction from CCF covers

`zk_con` builds the fixed Zuckerli variant for Figure 23 and Appendix C.3 of
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).
It reads the final BPA+ VByte clique cover (`.ccf`), forms sorted neighborhoods
as the encoder needs them, and writes Huffman-compressed rows. The paper runs
reuse the vertex order selected by `wg_con` for each graph.

The reference choice, copied blocks, residual gaps, zero runs, hybrid integers,
and Huffman coding follow the [Zuckerli paper](https://arxiv.org/abs/2009.01353).
Our files include a header and a separate row index, so the original Zuckerli
program cannot read them. See the [paper mapping](docs/paper-mapping.md),
[upstream fixes](docs/upstream-fixes.md), and [file format](docs/format.md).

## Fixed settings

| Quantity | Value |
| --- | ---: |
| Previous rows considered for a reference, `W` | 128 |
| Maximum reference-chain length, `R` | Unlimited |
| Degree/reference reset interval, `C` | 1 vertex |
| Minimum zero run, `L'` | 3 |
| Hybrid integer `(k, i, j)` | `(3, 1, 0)` |
| Reference-selection rounds | 2 |
| Entropy code | Huffman, at most 8 bits per symbol |

Each graph uses the selected WebGraph order, either its original order or the
saved BFS+LLP permutation. The encoder uses the same settings for all 48
graphs.

## Build and encode

Build with GNU Make and a C++17 compiler. The wrapper and tests require Python
3.6 or later.

```bash
make
make test
mkdir -p output
./bin/zk_con encode graph.ccf output/name
```

The command writes `output/name.zk` and `output/name.zki`. To use a saved
vertex order, add `--perm graph.perm` and place that file beside the outputs
as `output/name.perm`. The permutation maps encoded vertex IDs back to their
original IDs. The experiment wrapper `run.py` copies it into the bundle
automatically.

The encoder checks that the neighborhoods contain `2m` entries, where `m`
is the number of edges in the CCF header. `make test` checks file headers,
row index structure, deterministic encoding, and CSV export on small fixtures.
To compare every decoded row with the source CCF, use `zk_check` from
[`zk_algo`](../zk_algo/README.md).

The complete bundle size is `.zk + .zki`, plus `.perm` when the WebGraph order
uses one. Count the index and permutation in size comparisons.

## Negishi runs

From this directory, run `make` once, then `bash submit.sh`. The submission
creates one Slurm array task for each name in `graphs.txt`. Each task uses
the job settings in `run.sh` and reads
`$RCAC_SCRATCH/fall2026/format_ccf/<graph>.ccf` and the selected WebGraph
`manifest.json` under `$RCAC_SCRATCH/fall2026/format_wg/<graph>/`. The wrapper
checks the CCF header checksum against the WebGraph manifest and reads the
saved permutation for reordered graphs. It writes the bundle and its own
`manifest.json` under `$RCAC_SCRATCH/fall2026/format_zk/<graph>/`.
For other directory layouts, pass `--graph`, `--ccf-dir`, `--wg-dir`,
`--output-dir`, and `--encoder` to `python3 run.py`.

Reference selection examines up to 128 earlier rows in each of two rounds.
That can take substantial time on the largest graphs. Progress messages in the
Slurm log show the current round and vertex. A result without a final manifest
is incomplete.

After construction, export one timing row per graph from the final manifests:

```bash
python3 summarize_times.py \
  --output-dir "$RCAC_SCRATCH/fall2026/format_zk" \
  > encoding-times-and-sizes.csv
```

The script reads `graphs.txt` in order and writes `status=missing` when a graph
has no final manifest. It checks the file sizes against the manifest. Figure 23
uses `bundle_bytes` and `encode_seconds` from this CSV. The wrapper timer starts
after the wrapper's input checks and permutation copying. It covers the encoder
subprocess, including CCF loading, neighborhood construction, both reference
selection rounds, and writing `.zk` and `.zki`. Output hashing and publication
of the bundle happen after the timer stops. The WebGraph order is already
available when the wrapper is invoked.

## Source layout

| File | Role |
| --- | --- |
| `src/ccf_graph.*` | CCF cliques, optional permutation, sorted neighborhoods |
| `src/encode.*` | Reference choice and block/residual token generation |
| `src/codec.*` | Hybrid integers, contexts, Huffman tables, bit output |
| `src/format.*` | Graph header and packed row index |
| `run.py`, `run.sh`, `submit.sh` | One fixed candidate per graph on Negishi |
| `summarize_times.py` | Export final bundle sizes and construction times from manifests |
