# The five algorithms

Every row is the sorted neighborhood of a vertex in a simple undirected
graph. Each edge appears in both endpoint rows. Matching's first pass and
WVC use `u < v` to examine an edge once. `Graph::decode_row(u, row)` fills a
complete row before an algorithm examines it. The caller reuses `row`; no
algorithm keeps earlier adjacency lists.

Matching, BFS, DFS, and weighted vertex cover use the same steps as
`wg_algo`. Both vertex-weighted matching implementations use the same
maximum-gain rule and have their own tie rules.

## Matching: Figures 5 and 6

The first pass visits free vertices `u` in encoded-ID order and matches `u`
to the first free neighbor `v > u`, if one exists. It produces a maximal
matching `M0`. `matching_variant = "maximal"` returns its size.

For the 3/2 approximation, visit each original edge `{u,v}` of `M0` once.
Decode `u` and `v` in turn, collecting at most two distinct currently free
neighbors from each. If distinct free vertices `x` and `y` exist, replace
`{u,v}` by `{u,x}` and `{v,y}`. The second choice resolves the case where
the first choices coincide. The decoder still completes each row even when
the algorithm needs only one or two neighbors. No graph-wide candidate lists
or decoded adjacency lists are stored.

The set of free vertices only shrinks. An original matching edge that cannot
be augmented will not become augmentable later; a new matching edge touches
a vertex free in `M0`, which had no free neighbor. Thus the final matching
has no augmenting path of length one or three, as in the paper's proof.

## BFS: Figure 9

Start at each undiscovered vertex in encoded-ID order. Pop from a FIFO queue,
decode its row, and enqueue each undiscovered neighbor. Each new root is one
component. The state is visited marks, the queue, and the reusable row.

## DFS: Figure 10

Start at each undiscovered vertex. On entering `u`, decode its row once and
place undiscovered neighbors in `u`'s pending-child list. If a vertex is
pending under an active ancestor, move it to `u`'s list. Descend to a pending
child; when none remain, finish `u` and return to its parent. A pending
vertex can only move deeper on the active path. This yields a DFS forest
without retaining ancestor rows or decoding them again after return.

Four 32-bit arrays and one state byte per vertex maintain parents and
pending-child links, about `17n` bytes; the decoder also uses temporary row
buffers and reference-chain state. The report gives only the number of roots.
The [original Zuckerli stack traversal](upstream-audit.md) is not used because
it does not build a DFS forest.

## Weighted vertex cover (Figure 18)

Set `residual[v] = weight[v]`. For each uncovered edge `{u,v}`, subtract
`min(residual[u], residual[v])` from both endpoints. A vertex enters the cover
when its residual first reaches zero; add its original weight to the sum.
The algorithm skips `u`'s row if `residual[u]` is already zero. Otherwise the
decoder completes the row before the algorithm scans it, even if `u` becomes
tight partway through that scan.

Default weights attach to canonical CCF IDs `0, ..., n-1`: `mt19937_64` with
seed `19u` draws one integer in `[1,n²]` per vertex using rejection sampling.
A config may supply one positive integer per canonical vertex through
`weights_file`. If `.perm` exists, entry `perm[encoded]` gives the canonical
ID; weights are reordered before the algorithm timer. The result is an exact
decimal string, with unsigned 128-bit accumulation.

## Vertex-weighted matching: Pettie--Sanders Random-Match

`mvm` follows [Random-Match, Figure 1 and Lemma 3.3](https://web.eecs.umich.edu/~pettie/papers/twothirds.pdf).
It begins with an empty matching. In each sample it chooses one encoded vertex
uniformly with replacement and applies the largest **positive-gain**
2-augmentation centered there. With vertex weights, edge `{u,v}` has weight
`weight[u] + weight[v]`; the matching's edge-weight sum is exactly the sum of
its matched vertex weights. A separate `mt19937_64` generator chooses centers.
Its seed defaults to `1` and the same rejection mapping as the weight generator
avoids modulo bias. `MVM_SEED` overrides `mvm_seed` in the config, which lets
the Slurm scripts assign seeds `1, ..., 5` to five independent jobs.

Only four positive-gain alternating path forms remain after specializing the
paper's edge-weighted 2-augmentations to positive vertex weights:

| Edges | Path | Gain in matched vertex weight |
| --- | --- | --- |
| 1 | free `a` -- free `b` | `w(a) + w(b)` |
| 2 | free `a` -- `b` -- `c`, with `bc` matched | `w(a) - w(c)` |
| 3 | free `a` -- `b` -- `c` -- free `d`, with `bc` matched | `w(a) + w(d)` |
| 4 | free `a` -- `b` -- `c` -- `d` -- `e`, with `bc` and `de` matched | `w(a) - w(e)` |

An alternating four-cycle has zero gain. A five-edge alternating path that
starts and ends with matched edges removes three matched edges and adds two;
it releases two positive-weight vertices and has negative gain. A five-edge
path between free endpoints adds three edges, so it lies outside a
2-augmentation. If the sampled vertex is free, scan its row for the heaviest free
neighbor; such a one-edge path beats any two-edge path. If it has no free
neighbor, choose the matched neighbor whose mate is lightest and use the
two-edge path only when its gain is positive.

If the sampled vertex `v` is matched to `p`, decode `v`'s row and then `p`'s
row. On each side retain the two heaviest free neighbors and the matched
neighbor whose mate is lightest. The two free choices matter when the sides
share their heaviest free neighbor: a path cannot use it twice. Compare the
best paths of lengths two, three, and four, then flip the winning path. Ties
among neighbors on the same side favor the smaller encoded ID. When candidate
paths have the same positive gain, Zuckerli chooses the path with the
lexicographically smallest sequence of encoded IDs starting at its unmatched
endpoint. WebGraph compares the neighbors chosen on the sampled vertex's side
and its partner's side, in that order, followed by the IDs of any vertices
released from their matches. It orders an unused side before one that adds an
edge. Both rules choose a move with the same immediate gain, and the chosen
edges can affect later samples.

Only the `mate[n]` array and one reusable row survive between queries. Gains
use signed 128-bit arithmetic. The reported nonnegative total uses unsigned
128-bit arithmetic.

The algorithm counts matched vertices as paths are flipped. A one- or
three-edge path adds two matched vertices; a two- or four-edge path preserves
the count. If all `n` vertices are matched, every positive vertex weight is
already in the objective, so the matching is optimal and sampling stops.
This check uses one integer and does not scan `mate` after each sample.

The sample limit is

`ceil((5n/6) * log((1.5 + epsilon) / epsilon))`.

Lemma 3.3 gives expected weight at least
`(2/3)(1 - exp(-6k/(5n))) OPT`, so the chosen limit gives expected weight
at least `OPT/(1.5 + epsilon)`. The default `epsilon = 0.01` gives about
`4.1811n` samples. This is an **expectation**, unlike the deterministic
cover-parsimonious guarantee. The implementation runs all samples unless a
Slurm warning requests a partial report or all vertices become matched. An
all-matched early stop is an exact optimum and reports `complete`; a partial
run has a valid matching but does not inherit the full sample limit's
approximation bound.
