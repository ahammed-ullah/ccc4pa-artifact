# From the Zuckerli paper to this encoder

This construction follows the graph-list coding steps in [*Zuckerli: A New
Compressed Representation for Graphs*](https://arxiv.org/html/2009.01353),
Sections 2 and 3. It makes one fixed choice of parameters for the CCF
comparison. The resulting `.zk` and `.zki` files are a modified format;
the original Zuckerli program cannot read them. `zk_con` writes these files
but does not implement graph access or algorithms on them.

## Fixed choices

| Paper symbol | This encoder | Where to look |
| --- | --- | --- |
| Window `W` | Compare each row with up to 128 preceding rows. | `window` in [`format.hpp`](../src/format.hpp); `choose_references` in [`encode.cpp`](../src/encode.cpp) |
| Maximum reference-chain length `R` | Unlimited. The encoder uses the estimated coding cost to choose a reference within the window. | `choose_references` in `encode.cpp` |
| Degree chunk size `C` | 1: store every degree directly and reset the previous-reference context for each row. | `chunk_size` in `format.hpp`; `make_row_tokens` in `encode.cpp` |
| Zero-run threshold `L'` | 3: after three consecutive zero residual gaps, replace further zeros with a run-length value. | `zero_run_min` in `format.hpp`; `emit_residuals` in `encode.cpp` |
| Hybrid integer `(k,i,j)` | `(3,1,0)`: values 0–7 have direct symbols; larger values place an exponent and one high bit in the Huffman symbol, then write the remaining bits raw. | `split_integer` in [`codec.cpp`](../src/codec.cpp) |
| Reference-selection rounds | 2. Start with a flat symbol cost, then estimate symbol costs from the first round's selected rows. | `encode_graph` in `encode.cpp` |
| Entropy coder | One length-limited Huffman code for each of Zuckerli's 214 contexts, with an eight-bit maximum code length. ANS is not used. | `HuffmanTables` in `codec.cpp` |

These are **our** fixed settings. The paper's principal list-access
experiments use `R=3`, `C=32`, `W=32`, and `(k,i,j)=(4,1,0)`; its parameter
study also tests `(3,1,0)` and windows through 64. Our larger window and
unlimited chains aim at compressed size, while `C=1` gives each row its own
bit range. These choices can change both size and future row-access time, so
results must be labeled with them. See the paper's [Sections
3.2–3.4](https://arxiv.org/html/2009.01353#S3) and
[4.2](https://arxiv.org/html/2009.01353#S4.SS2).

## What happens to one graph

1. [`CcfGraph`](../src/ccf_graph.cpp) reads the final clique cover. For a
   vertex, it merges the sorted member lists of its incident cliques, removes
   repeated neighbors from overlapping cliques, and produces one sorted open
   neighborhood. Isolated vertices remain in the graph. An optional `.perm`
   maps encoded IDs back to canonical CCF IDs. This replaces upstream's
   prebuilt adjacency-array input without changing the encoded graph.
2. In each round, [`choose_references`](../src/encode.cpp) first scores the
   row with no reference, then scores up to 128 earlier rows from nearest to
   farthest. It estimates the cost of the copied blocks and
   residual neighbors, including the raw bits used for hybrid integers. It
   switches to a candidate only when its estimate improves by more than
   `1e-6`. It keeps up to 129 rows and an `n`-entry reference array.
3. With a reference, `split_row` divides the reference list into alternating
   copied and skipped blocks and places uncopied neighbors in the residual
   list. The first block length is stored as-is, later lengths are stored
   minus one, and the final block length is implicit. Without a reference,
   every neighbor is a residual. This is the block-copy step of Section 3.2.
4. `emit_residuals` stores the first residual as a signed distance from the
   current vertex. Later gaps subtract positions supplied by copied blocks.
   Runs of zero gaps use the `L'=3` rule. Contexts follow Section 3.3:
   separate contexts for block roles, a first-residual context chosen from
   the residual count, and later contexts chosen from the previous delta.
5. After round 1, the encoder counts the block and residual tokens kept for
   the selected rows and updates its cost model. After round 2, it counts
   all tokens, including degrees and reference distances, to build the final
   Huffman tables. It then writes one row's tokens at a time.
   [`codec.cpp`](../src/codec.cpp) writes each hybrid symbol and its raw
   extra bits. Its table layout and eight-bit package-merge length limit
   follow the upstream Huffman implementation.
6. [`format.cpp`](../src/format.cpp) writes a 64-byte `.zk` header with the
   fixed settings, graph counts, and fingerprint. The `.zk` file then
   contains Huffman tables and row bitstreams. The `.zki` file stores `n+1`
   absolute bit offsets in a packed array. A reordered run also needs its
   `.perm` file to recover canonical IDs; count that file in bundle size.

The paper's bounded-`R` list-access guarantee does not apply to this
unlimited-chain choice: a row may depend on a long chain. Decoding and
graph-level validation are provided by [`zk_algo`](../../zk_algo/README.md).
The complete bundle is `.zk + .zki`, plus `.perm` when a different vertex
order is used.
