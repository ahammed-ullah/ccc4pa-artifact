# LZF Implementation

The complete first-fit variant of Lovasz's construction is in
`src/algo_lzf.cpp`. Definition 3.12 uses maximum-clique peeling, and the
paragraph that follows describes the first-fit alternative implemented by LZF
in [*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).

See [Construction Guide](constructions.md) for common notation.

## Construction

The paper's family `F` is represented by:

- `h[v]`, the first-fit class containing vertex `v`;
- `F[ell]`, the vertices in class `ell`;
- `S_(v,ell) = F[ell] intersect N(v)` for every earlier class
  `ell < h[v]` with a neighbor of `v`.

Every `F[ell]` is a clique in the input graph because it is a color class of
the complement. After computing these classes, LZF performs the two steps of
Definition 3.12:

1. Add every non-singleton `F[ell]` to `C`.
2. For each vertex `v` and each nonempty earlier restriction
   `S_(v,ell)`, add `S_(v,ell) union {v}` to `C`.

The shared first-fit routine and the LZF loop both use increasing canonical
vertex IDs. The restrictions for one vertex are emitted in increasing `ell`
order. This fixes the output order for repeated runs on the same canonical
graph.

## First fit without constructing the complement

`first_fit_complement(adj,h,F)` tests a complement color through the input
graph. While processing `v`, it scans the already processed neighbors `u<v`
and counts how many belong to each existing class. Class `ell` accepts `v`
exactly when that count equals `|F[ell]|`, because then `v` is adjacent in the
input graph to every member of the class. First fit chooses the smallest
accepting label.

The routine records which counters were touched for the current vertex, so it
does not clear every class counter before processing the next vertex. LZF calls
this routine inside `construct_lzf`, and its reported construction time
therefore includes the first-fit coloring. FPA calls the same routine inside its
own timed construction.

## Forming the restricted sets

For a fixed `v`, the nonempty sets `S_(v,ell)` are disjoint and together contain
all lower-class neighbors of `v`. The implementation forms this partition
directly instead of separately intersecting every complete `F[ell]` with
`N(v)`.

The first scan of `N(v)` counts neighbors by their first-fit label. The source
sorts the labels it found, creates one output clique per label, and reserves
its storage. The second scan appends neighbors directly to those cliques.
Since `N(v)` is sorted, each restriction is already in sorted order. The
source inserts `v` and resets the counters and output indices for the labels
it used.

Thus each neighborhood is scanned twice, and only labels that actually occur
at a vertex are sorted. The total construction time is `O~(m)`, and the code
never stores all sets `S_(v,ell)` at once.

## Cover properties

LZF does not test whether an edge is already covered, call `AugmentCover`,
maintain admissibility sets, or run phases. An edge may therefore occur in a
cross-class clique even when an earlier clique already covers it. In general,
the resulting cover is not composition-minimal; this is the redundancy that
motivates FirstFit-PA in the paper.

Each emitted clique can nevertheless be charged to a distinct nonempty set of
graph edges. An initial class owns its internal edges, and
`S_(v,ell) union {v}` owns the edges from `v` to `S_(v,ell)`. These edge sets
are disjoint, so LZF emits at most `m` cliques and satisfies
`sigma(C) <= 2m`.

LZF has no construction parameters and no additional statistics. Its
`algorithm_statistics` JSON object is empty.

**Assignment-minimal warning.** LZF has `O~(m)` construction time, whereas
assignment-minimal postprocessing can cost `O(dm)` or, using the coarser bound,
`O(nm)`. Enabling it therefore forfeits LZF's near-linear bound for the complete
run, although construction time remains reported separately. Evaluation runs
found only negligible reductions in LZF cover size, so this stage should
normally remain disabled for LZF.
