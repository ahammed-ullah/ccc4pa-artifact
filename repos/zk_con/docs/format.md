# Files written for one graph

For a graph named `g`, the runtime bundle is `g.zk` and `g.zki`, plus `g.perm`
if the selected WebGraph order is BFS+LLP. All three files count toward the
size result. The `manifest.json` records their lengths and SHA-256 hashes; it
is experimental metadata rather than part of the compressed graph.

The two binary files are a modified Zuckerli format. They carry the fixed
settings from [paper-mapping.md](paper-mapping.md). `zk_con` writes them;
[`zk_algo`](../../zk_algo/README.md) reads them and runs the graph algorithms.

## `g.zk`

The first 64 bytes are:

| Bytes | Value |
| --- | --- |
| 0–7 | ASCII `ZKCCF001` |
| 8–11 | Number of vertices `n`, unsigned 32-bit little endian |
| 12–15 | Reference window `W=128`, unsigned 32-bit little endian |
| 16–23 | Number of undirected edges `m`, unsigned 64-bit little endian |
| 24–29 | `C=1`, `L'=3`, hybrid `k=3,i=1,j=0`, two reference rounds |
| 30–31 | Huffman code ID 1, unlimited-chain flag 1 |
| 32–63 | Canonical graph fingerprint copied from the CCF header, 32 bytes |

After the header, the bitstream stores one Huffman table for each of Zuckerli's
214 contexts, followed by one encoded row for each vertex in encoded-ID order.
Bits within bytes are written from least significant to most significant. A
table records the highest symbol in eight bits, then a presence bit for each
symbol through that one and three bits for `code length − 1` when present.
Codes have at most eight bits. For each row, the degree is absolute because
`C=1`; a nonempty row after vertex zero then stores a backward reference
distance, possibly zero, followed by blocks and residuals. The final byte is
zero padded if needed.

The header's fingerprint identifies the **canonical graph**, so it does not
change when `.perm` changes the encoding order. `m` counts each undirected
edge once; the rows together contain `2m` neighbor entries.

## `g.zki`

The first 24 bytes are:

| Bytes | Value |
| --- | --- |
| 0–7 | ASCII `ZKIDX001` |
| 8–11 | Number of vertices `n`, unsigned 32-bit little endian |
| 12 | Bit width of every stored row offset, between 1 and 64 |
| 13–15 | Zero |
| 16–23 | End bit position of the final row, unsigned 64-bit little endian |

The rest is an array of `n+1` absolute bit positions in `g.zk`: one for each
row start and one for the final row end. Each value uses the same bit width,
chosen from the final end position, and values are packed least significant
bit first with no gaps. The encoder writes the first offset immediately after
the Huffman tables, writes offsets in row order, and records the final bit
position before padding the last byte. The `zk_algo` decoder checks these
relationships when it opens the files.

The index does not contain a second hash of `g.zk`. The run script records
full file hashes in the manifest, but it does not decode the graph or compare
rows with the CCF source. Keep the `.zk` and `.zki` from the same manifest
together. [`zk_check`](../../zk_algo/README.md) compares every decoded row
with the CCF source outside timed algorithm runs.

## `g.perm`

This optional file is copied from the selected WebGraph bundle. It is an array
of `n` little-endian unsigned 32-bit integers. Entry `u` is the canonical CCF
vertex ID represented by encoded vertex `u`. The array must be a permutation
of `0..n−1`. It is needed to interpret decoded rows in the original graph's
IDs and is included in the runtime size.
