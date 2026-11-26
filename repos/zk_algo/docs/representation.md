# Reading the Zuckerli representation

`zk_con` constructs sorted neighborhoods from the final CCF cover and writes
one compressed row per encoded vertex. The runtime bundle is `.zk` (Huffman
tables and rows) plus `.zki` (packed bit offsets of the `n+1` row boundaries).
If BFS+LLP reordered the graph, `.perm` maps encoded IDs back to canonical
CCF IDs. Count all present files in the bundle size. The exact header and
offset layout are in `zk_con/docs/format.md`.

`Graph::load` reads `.zk` and `.zki` into memory, checks their format, and
prepares the Huffman tables. It keeps the row index packed. The report counts
the bytes of `.perm` whenever it is present. Weighted vertex cover and
vertex-weighted matching read `.perm` during weight preparation to put the
original vertex weights in encoded order before the algorithm timer starts.

`Graph::decode_row(u, row)` gets `u`'s bit range from `.zki` and writes the
complete sorted neighborhood into the caller's reusable buffer. A row can
refer to an earlier row. The decoder follows that chain iteratively, then
reconstructs rows from the oldest needed reference to `u`. Each request uses
the caller's row buffer, temporary buffers for copied and residual neighbors,
and one metadata record per row in the chain. Only the caller's row buffer
survives the request. Each new request resolves its references again,
including requests for matching endpoints.

The 128-row window limits how far back each reference points. The encoder
sets no fixed limit on the total chain length, so one request can follow up
to `n−1` links. Buffer use depends on the largest rows encountered in the
chain. The row buffers and chain metadata contribute to process peak RSS.
The algorithm timer includes all reference resolution and row decoding.

This is a specific [Zuckerli paper](https://arxiv.org/html/2009.01353v1)
variant: `W=128`, unlimited `R`, `C=1`, `L'=3`, hybrid integers `(3,1,0)`,
and Huffman coding. The paper's main list-access experiments used other
settings. The external `.zki` and file header also differ from the original
binary format. The decoder accepts this variant and checks the recorded
settings; it does not silently interpret another Zuckerli file.
