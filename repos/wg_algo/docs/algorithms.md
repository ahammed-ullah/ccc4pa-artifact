# The five algorithms

All rows represent a simple undirected graph, so every edge appears in both
endpoint rows. The first matching pass and WVC consider an edge only from
the endpoint with the smaller ID. Every neighbor request decodes the complete
row into one reusable buffer. The algorithm then scans that buffer. Bulk mode
may use additional temporary rows within the call; neither mode retains them
for a later query. The decoder has no
matching, BFS, DFS, WVC, or MVM-specific stopping rule, and no earlier complete
row is retained for later queries.

## Matching: Figures 5 and 6

The first scan decodes each free vertex `u`'s row, then greedily matches `u`
to its first free neighbor `v > u`. The decoder finishes the row even though
the algorithm needs only one eligible neighbor. At the end, every edge has a
matched endpoint, so this is a maximal matching `M0`.
`matching_variant = "maximal"` reports its size.

For the 3/2 approximation, Figure 6 examines each edge `{u,v}` of `M0` and
looks for distinct currently free neighbors `x` of `u` and `y` of `v`. If
they exist, it replaces `{u,v}` with `{u,x}` and `{v,y}`. Two distinct free
choices per endpoint suffice to resolve the case where both first choices
are the same vertex. In that case the code tries the first endpoint's second
choice before the second endpoint's second choice. `wg_algo` decodes endpoint
rows as needed, using the same buffer, and collects up to two free choices
from each. Looking up these choices as needed avoids storing the paper's
`Gamma` lists, which could grow with the number of edges.

The code marks the vertices matched in `M0` so it can recognize edges of
that initial matching during the sweep. The set of free vertices only
shrinks as replacements occur, so an edge that could not be augmented cannot
become augmentable later. New matching edges touch vertices free in `M0`,
which had no free neighbors
because `M0` was maximal. Thus the final matching has no augmenting path of
length one or three, as in the paper's proof.

## BFS: Figure 9

BFS starts at each undiscovered vertex in increasing WebGraph ID order. It
pops a vertex from a FIFO queue, decodes its row, and enqueues each
undiscovered neighbor. Every new root is one connected component. Its queue,
visited marks, and one reusable row buffer are its traversal state. In the
cover implementation, visiting one clique can discover several neighbors
at once; both versions produce a BFS forest.

## DFS: Figure 10

DFS starts at each undiscovered vertex. On entering `u`, it decodes the whole
row once. Each undiscovered neighbor becomes a pending child of `u`; if an
active ancestor already owns that pending vertex, `u` moves it from the
ancestor's list. DFS then descends to a pending child. A descendant may in
turn claim another pending child of `u`.

A pending vertex can move only deeper along the active path. Thus `u` cannot
finish while an undiscovered neighbor remains, and every non-tree edge joins
an ancestor and a descendant. This is a DFS forest for some neighbor order.
The program reports only the number of roots.

The pending lists use four 32-bit arrays (heads, previous and next links,
and owner/parent) and one state byte per vertex: about `17n` bytes. DFS also
uses one reusable row buffer. It requests each vertex's row once and does not
replay that row after returning from a child. The WebGraph decoder may still
resolve earlier referenced rows while answering a new row query.

## Weighted vertex cover: Figure 18

`residual[v]` starts at the positive weight `w[v]`. For each uncovered edge
`{u,v}`, the algorithm subtracts `min(residual[u], residual[v])` from both
residuals. The edge is then covered by at least one tight vertex. A vertex
enters the cover when its residual first reaches zero, and its original
weight is added to the reported sum. This is the edge-by-edge primal-dual
form of Figure 18. `cp_algo` pairs positive-residual vertices within each
clique, so the two implementations can select different vertices.

If `residual[u]` is already zero, WVC has no need to request `u`'s row.
Otherwise it decodes the complete row before examining eligible edges.
The algorithm may stop scanning the buffer after `u` becomes tight; this
does not stop or change the decoder.

Default and file weights attach to original CCF vertex IDs. If `wg_con`
reordered the graph, `.perm` moves those weights into WebGraph order before
the timer. Thus both programs solve the same weighted instance. The default
generator and decimal sum format are specified in [weights.md](weights.md).

## Vertex-weighted matching: Pettie–Sanders Random-Match

`mvm` starts with an empty matching. For up to `k` independent samples, it
chooses one vertex uniformly with replacement and applies the highest
positive-gain 2-augmentation centered there. The edge `{u,v}` has weight
`w[u]+w[v]`, so the result is the sum of weights of matched vertices. The
algorithm follows [Pettie and Sanders, Random-Match, Figure 1 and Lemma
3.1](https://web.eecs.umich.edu/~pettie/papers/twothirds.pdf). It is a
standard graph matching algorithm; it does not use the cover algorithm's
clique operations.

For positive vertex weights, every useful centered augmentation has one of
these forms. `a` and `b` are free vertices, and `z` is a matched vertex
released by the change:

| Alternating path | Weight gained |
| --- | ---: |
| `a--b` (one edge) | `w[a]+w[b]` |
| `a--u--z` (two edges; `u--z` is matched) | `w[a]-w[z]` |
| `a--u--v--b` (three edges; `u--v` is matched) | `w[a]+w[b]` |
| `a--u--v--x--z` (four edges; `u--v`, `x--z` are matched) | `w[a]-w[z]` |

A four-cycle changes the matching edges while preserving its four matched
vertices, so its gain is zero. A path with three matched edges and two new
edges releases two positive-weight endpoints, so its gain is negative.

When the sampled vertex is free, one row gives its heaviest free neighbor and
the matched neighbor whose mate has the least weight. If a free neighbor
exists, the one-edge path always wins. When the sampled vertex is matched,
the algorithm requests its row and its mate's row in turn. It remembers the
two heaviest free neighbors on each side and the cheapest released endpoint
of a matched arm on each side. Two free choices are needed only when the
heaviest choice is the same vertex on both sides. These choices determine
the maximum gain among the four path forms. Equal weights favor the smaller
WebGraph ID of the neighbor. For full paths with equal gain, compare the
left and right neighbor IDs first, then the IDs of the released vertices,
and prefer the smaller ID at the first difference.
The algorithm updates `mate` and the exact 128-bit result only after choosing
the best valid path. It keeps no decoded row for a later sample.
It maintains the number of matched vertices as paths are flipped. If all
vertices become matched, it stops immediately: with positive vertex weights,
that matching has the maximum possible weight. This adds no graph scan.

For epsilon `e`, Lemma 3.3 gives
`E[weight] >= (2/3)(1-exp(-6k/(5n))) OPT`. The default `e=0.01` sets
`k=ceil((5n/6) log((1.5+e)/e))`, about `4.1811n` samples. This yields an
expected weight of at least `OPT/(1.5+e)`, matching the cover algorithm's
numerical approximation factor. The cover guarantee holds for each run;
Random-Match gives the stated bound in expectation. Stopping before `k`
because all vertices are matched produces an exact optimum. A run stopped
by Slurm before `k` samples still has a valid matching, but the fixed-`k`
bound does not apply to that partial run.
