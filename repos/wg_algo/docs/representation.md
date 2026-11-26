# Reading the WebGraph representation

`wg_con` starts from the same graph as the final CCF cover and writes a
standard BVGraph file. A row describes one vertex's sorted neighbors. The
selected runtime bundle contains `.graph` (compressed rows), `.properties`
(counts and codes), and `.ef` (compressed bit positions of row starts).
`.offsets` was used to build `.ef` and is not part of the runtime bundle.
When `wg_con` reorders vertices, `.perm` maps each WebGraph ID back to its
original CCF ID.

Before the algorithm timer, `wg_algo` reads the compressed bytes into memory.
The timer therefore does not include shared-filesystem reads. No complete
adjacency representation is built. All five algorithms request neighbors
through `Graph::decode_row(u, row)`. This function finds `u`'s bit position
through `.ef` and fills `row` with the complete, sorted neighbor list before
the algorithm examines it. The caller reuses the same `vector<int>` for its
next request. The selected decoder does not change any algorithm loop.

A WebGraph row may copy successors from an earlier row and add intervals or
residual successors. The encoder has already chosen the reference and marked
which parts of it to copy. Both decoders follow those instructions from the
same `.graph` and `.ef` files. They differ only in how they resolve references
within a call:

- `bulk` (the default) follows the chain to a row without a reference,
  then builds each referenced row in turn. It copies selected runs from the
  completed parent row and merges them with that row's intervals and
  residuals. It reuses a constant number of complete-row buffers while
  answering this request.
- `iterator` keeps a position in each active reference row. It produces the
  requested row one successor at a time, pulling values through the chain
  when needed. Only the requested row is materialized.

Both modes discard reference-chain state when the request ends; neither keeps
a window or cache of decoded rows for later requests. The bulk mode can use
more temporary memory because it materializes intermediate rows. The iterator
mode can spend more CPU time advancing nested reference states. The selected
`wg_con` encodings permit unlimited reference depth, so chain metadata also
depends on the actual chain. These costs are included in process peak RSS and
algorithm time. Every `decode_row` call, including repeated requests for a
matching endpoint, is timed.

The paper's cover algorithms obtain graph neighbors by reading cliques.
`wg_algo` obtains the same graph neighbors from WebGraph rows. The paper's
approximation proofs use graph edges and free vertices, not a particular
encoding. BFS and DFS here perform actual traversals, although only the
number of forest roots is reported.

The C++ iterator decoder follows the [random-access BVGraph row decoder](https://docs.rs/webgraph/0.6.2/src/webgraph/graphs/bvgraph/random_access.rs.html)
in the pinned Rust WebGraph 0.6.2 release. Bulk mode reconstructs the same
encoded rows from oldest reference to requested row; it is an alternative
decoding procedure for the same WebGraph representation. Neither mode uses
the sequential iterator's window of previously decoded rows. Current `wg_con` searches
standard `BvComp` with
big-endian bit order, gamma codes for outdegrees, references, blocks, and
intervals, and zeta-3 for residuals. The C++ reader checks the properties
and rejects settings it does not implement. It does not silently reinterpret
another WebGraph format.
Its `.ef` reader targets the 64-bit, little-endian Elias--Fano file written
by the pinned WebGraph 0.6.2 toolchain on Negishi. The integration test
compares emitted arcs against that toolchain, including offsets on both
sides of the index's 4096-row inventory boundary.
