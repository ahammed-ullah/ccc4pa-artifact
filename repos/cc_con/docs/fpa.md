# FPA Implementation

`src/algo_fpa.cpp` implements FirstFit-PA (Figure 2) and AugmentCover (Figure 3).
Lemmas 3.16 and 3.18 give the cover and resource bounds in
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).

The implementation follows the pseudocode with one important representation
choice: it does not store the uncovered-neighbor sets `M_v`. It stores the
labels of cliques containing each vertex and uses those labels to decide whether
an edge is covered. Common notation is defined in the
[Construction Guide](constructions.md).

## First-fit classes

The shared `first_fit_complement` procedure processes vertices in canonical
vertex-ID order. It returns:

- `h[v]=k`, where `F[k]` is the first-fit class containing `v`;
- `F[k]`, a clique in the input graph.

These are first-fit class labels, not output-cover labels. The source uses
`ell` for a class earlier than `F[h[v]]` and `q` for an output-cover
label.

Following Steps 3--5 of Figure 2, FPA places every non-singleton `F[k]` in the
initial cover. For each such output clique `C[q]`, it constructs

```text
D[q] = intersection_{z in C[q]} N[z]
```

and inserts `q` in `A[w]` for every `w` in `D[q]`. Singleton first-fit
classes remain available through `h` and `F`, but they are not copied into
the cover because they cover no edge.

The code intersects the first two vertices' adjacency lists and inserts those
two vertices. For each remaining clique member, it filters the surviving
entries in place, using binary searches for adjacency tests. The proof of
Lemma 3.18 obtains `O(m)` initialization by scanning each vertex's neighbors
and grouping them by first-fit class.

## The sets used by Figure 2

The implementation stores:

- `C[q]`: output clique `q`;
- `L[v]`: all output labels `q` for which `v` belongs to `C[q]`;
- `A[v]`: current-phase labels that may accept `v`;
- `D[q-first_label]`: the admissible vertices for a label belonging to the
  current phase.

`C` and `L` remain throughout the construction. `A` and `D` are
discarded at a phase boundary.

Figure 2 uses `M_v`, the currently uncovered neighbors of `v`. The source
represents the same condition by

```text
u is in M_v
    if and only if
u is in N(v) and L[u] intersect L[v] is empty.
```

This equivalence is exact because `L` is updated whenever a vertex is added to
an output clique. It avoids storing a second copy of the graph and deleting
entries from `M_v` whenever one clique covers many edges.
Each test scans comparable membership lists together or binary-searches for
labels from the shorter list in the longer one. Its cost depends on the
membership-list lengths. The proof accounts for coverage through explicit
`M_v` sets.

## Constructing the pairs (v,ell)

For a fixed vertex `v`, let `k=h[v]`. Figure 2 defines

```text
T_v = {ell < k : F[ell] intersect N(v) is nonempty}.
```

The work associated with `(v,ell)` uses the restricted class
`F[ell] intersect N(v)`. The source constructs all nonempty restrictions for
one `v` together:

1. Scan `N(v)` and count lower-class neighbors by their class label `ell`.
2. Sort the labels that occur and convert the counts into consecutive ranges
   in one reusable `bucket_members` vector.
3. Scan `N(v)` a second time and place every lower-class neighbor in its
   range.

Because `N(v)` is sorted by vertex ID, each range is already sorted. For a
fixed `v`, these ranges partition its lower-class neighbors, so the two scans
take `O(deg(v))` total work. The ranges are discarded before the next vertex.
The implementation never stores all pairs `(v,ell)` or all restricted classes
at once.

For the current range, filtering its members through
`L[u] intersect L[v]` gives

```text
F[ell] intersect M_v,
```

which is the set used to form `E'` in Step 7(a)(i). Only its uncovered edges
are sent to AugmentCover.

## AugmentCover

For an uncovered edge `{u,v}`, the code follows Figure 3:

1. If `A[u] intersect A[v]` is nonempty, choose one label `q` and add any
   missing endpoints to `C[q]`.
2. Add `q` to `L[u]` or `L[v]` when that endpoint newly enters the clique.
3. Intersect `D[q-first_label]` with the closed neighborhood of each newly
   added endpoint. Remove `q` from `A[w]` whenever `w` leaves this
   admissible set.
4. If no label accepts the edge and the phase is open, create the edge clique
   `{u,v}` and initialize its `D` and `A` entries.
5. If no label accepts the edge and the phase is closed, leave it for a later
   phase.

After the strict condition `sigma(D)>tau*m` becomes true, the clique causing
the overflow remains active, but the current phase creates no more cliques.

## Deferred pairs

If an edge from `(v,ell)` reaches the closed-phase branch, FPA checks that
restricted class again after all its AugmentCover calls. The pair is retained
only if it still contains an uncovered edge, as required by Step 7(a)(iii).
Only the two integers `v` and `ell` are retained.

In a later phase, retained pairs with the same `v` are consecutive. Their
restricted classes are rebuilt together with the same two scans of `N(v)`
used in the first phase. Thus a later phase does not separately intersect a
complete `F[ell]` with `N(v)` for every retained pair.

At the phase boundary, FPA clears `A` and `D`, keeps `C`, `L`, `h`,
and `F`, and starts the new phase's output labels at the current `|C|`.

## Choosing a label

Figure 3 permits any label in `A[u] intersect A[v]`. FPA offers the same four
deterministic choices as BPA:

- `min-label`: smallest common label;
- `max-label`: largest common label;
- `min-clique`: label of a smallest current clique;
- `max-clique`: label of a largest current clique.

Clique-size ties select the smallest label.

The source uses a direct two-pointer scan of the sorted `A` vectors and does
not build their intersection. The analysis in Lemma 3.18 charges this operation
by the skew-aware cost `O~(min{|A[u]|,|A[v]|})`. A two-pointer scan instead
has worst-case cost `O(|A[u]|+|A[v]|)`. It was retained because evaluation
showed it to be faster in practice through contiguous reads and early matches.
This deliberate implementation choice does not itself realize that particular
set-intersection cost used in the lemma's analysis.

Individual removals from `A[v]` use `lower_bound` followed by
`vector::erase`, which keeps the stored relation compact and can shift later
labels in that list.

## Bounds and reported values

Lemma 3.16 proves that FPA returns a concise cover with

```text
|C| = O(min{m,mu})
sigma(C) = O(min{m,n*mu}).
```

Lemma 3.18 gives `O~(d^2 min{m,mu})` time and `O(m)` space for the analyzed
construction. The reported time measures the implementation described here,
including closed-neighborhood initialization, membership-list coverage tests,
and sorted-vector admissibility operations.

`tau` defaults to `4.0` and must be at least one. `num_phases` counts
passes that contain at least one nonempty pair `(v,ell)`. It is zero when the
initial first-fit cliques already account for all relevant work.
`peak_size_D` is the largest value of `sigma(D)`, including the
admissibility sets of the initial first-fit cliques. Generic report fields give
the final number of cliques, total cover size, and construction time.
