# Audit of the original Zuckerli traversal code

This audit refers to the [published Google Zuckerli source at commit
`874ac407`](https://github.com/google/zuckerli/tree/874ac40705d1e67d2ee177865af4a41b5bc2b250)
and to [*Zuckerli: A New Compressed Representation for Graphs*](https://arxiv.org/html/2009.01353v1).
The graph format written by `zk_con` has a different header, an external
packed `.zki` index, and different fixed settings. The original binary
cannot read it. `zk_algo` follows the paper's graph operations and uses its
own decoder. [Encoder defects and fixes](../../zk_con/docs/upstream-fixes.md)
are documented with `zk_con`; this audit covers traversal and decoding.

## What the published traversals do

The [compressed BFS](https://github.com/google/zuckerli/blob/874ac40705d1e67d2ee177865af4a41b5bc2b250/src/traversal_main_compressed.cc#L15-L46)
starts a queue search from every unvisited vertex and calls `Neighbours(u)`
once when `u` leaves the queue. This is a BFS forest.

The [function named DFS](https://github.com/google/zuckerli/blob/874ac40705d1e67d2ee177865af4a41b5bc2b250/src/traversal_main_compressed.cc#L48-L81)
uses a stack, but pushes and marks **all** currently unvisited neighbors
before descending. A sibling marked at that point cannot be claimed by a
deeper vertex, so the resulting search need not be a DFS forest. It still
visits every vertex reachable from a root, so its root count is a valid
component count on these undirected graphs. `zk_algo` uses the pending-child
method in [algorithms.md](algorithms.md) for an actual DFS forest.

Neither upstream traversal reports its component count. `num_visited` in
both functions and `count` in DFS are incremented but unused; the program
prints elapsed milliseconds and optionally the vertex order. Those unused
counters can disappear under optimization. `zk_algo` returns and reports the
component count, which also makes the measured result observable.

## Decoding and memory

The original [`CompressedGraph` constructor](https://github.com/google/zuckerli/blob/874ac40705d1e67d2ee177865af4a41b5bc2b250/src/compressed_graph.cc#L15-L40)
reads the whole compressed file and [decodes every row once](https://github.com/google/zuckerli/blob/874ac40705d1e67d2ee177865af4a41b5bc2b250/src/decode.h#L18-L46)
to discover row starts. It retains an 8-byte `size_t` offset per vertex, but
not all adjacency rows. The scan temporarily keeps a bounded window of
reference rows. `zk_algo` reads the `.zki` index created during encoding, so
it does not need that full scan on open.

The original [`Neighbours(u)`](https://github.com/google/zuckerli/blob/874ac40705d1e67d2ee177865af4a41b5bc2b250/src/compressed_graph.cc#L82-L135)
returns a new complete row vector and recursively calls itself for referenced
rows. There is no persistent decoded-row cache. The paper explicitly says
that [lazy list iteration was not implemented](https://arxiv.org/html/2009.01353v1#S4.SS5).
Our unlimited reference chains could exhaust a recursive call stack, so
`zk_algo` resolves them iteratively and discards temporary rows after each
request.

Both upstream traversal functions take `CompressedGraph` **by value**.
The [caller constructs the graph](https://github.com/google/zuckerli/blob/874ac40705d1e67d2ee177865af4a41b5bc2b250/src/traversal_main_compressed.cc#L83-L91)
before calling one, and each [timer starts inside the function](https://github.com/google/zuckerli/blob/874ac40705d1e67d2ee177865af4a41b5bc2b250/src/traversal_main_compressed.cc#L15-L21).
Thus file loading, the full index-building scan, and the by-value copy of
compressed bytes and offsets occur outside its reported traversal time. A
whole-process peak RSS measurement would include those allocations. The
[paper's Table 9](https://arxiv.org/html/2009.01353v1#S4.SS5) reports memory
in MB but does not specify enough measurement procedure to tell whether its
numbers include the temporary copy.

Finally, the upstream [row reader passes the full file length](https://github.com/google/zuckerli/blob/874ac40705d1e67d2ee177865af4a41b5bc2b250/src/compressed_graph.cc#L82-L85)
to `BitReader` after advancing its pointer to the row. The
[`BitReader` refill bound](https://github.com/google/zuckerli/blob/874ac40705d1e67d2ee177865af4a41b5bc2b250/src/bit_reader.h#L51-L59)
then extends past the allocation near the file end. `zk_algo` must bound
each row by its `.zki` end bit and reject truncated or inconsistent data.
