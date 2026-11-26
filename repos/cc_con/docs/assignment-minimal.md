# Assignment-Minimal Postprocessing

Assignment-minimal postprocessing is an optional stage applied after a clique
cover has been constructed. It is outside every construction's timed call and
modifies the cover in place. The complete procedure is in
`src/assignment_minimal.cpp`. It implements AM-PostProcess from Figure 16 of
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).
Lemma A.5 proves correctness and gives the size-sensitive resource bounds.

For a cover `C=(C[0],...,C[p-1])`, an assignment is a pair `(v,C[ell])` with
`v` in `C[ell]`. The cover is assignment-minimal if removing any one assignment
leaves some graph edge uncovered. This local property is distinct from
assignment-optimality, which minimizes `sigma(C)=sum_ell |C[ell]|` over all
possible covers.

## Greedy procedure

The implementation considers vertices in increasing ID order and, for each
vertex, considers its clique labels in increasing order. It uses:

- `L[v]`, the labels of cliques containing `v` when its iteration begins;
- `multiplicity[u]`, for a fixed current vertex `v`, the number of current
  cliques containing both `u` and `v`.

Thus, while processing `v`,

```text
multiplicity[u] = |{ell in L[v] : u is in the current C[ell]}|.
```

The procedure is:

1. Sort and deduplicate every `C[ell]`, and construct all membership lists
   `L[v]` by visiting labels in increasing order.
2. For the current vertex `v`, scan the cliques named by `L[v]` to compute the
   nonzero multiplicities.
3. Consider each `ell` in `L[v]`. The assignment `(v,C[ell])` is removable
   exactly when `multiplicity[u] >= 2` for every `u` in `C[ell]` other than
   `v`.
4. When the assignment is removed, delete `v` from `C[ell]` and decrement
   `multiplicity[u]` for every remaining `u` in that clique.
5. After all vertices have been processed, discard empty and singleton cliques,
   which cover no edge, and move the remaining cliques together in `C`.

`L` is constructed once rather than maintained after every removal. Before
vertex `v` is processed, earlier iterations have removed only their own
vertices, so they cannot have made `L[v]` inaccurate. During the iteration,
removed labels are allowed to remain in `L[v]`: every label is considered only
once, and `L[v]` is never read again afterward. A later vertex obtains current
membership information from the modified cliques in `C`, not from `L[v]`.

## Correctness

Every removal preserves edge coverage. The removal test requires another
current clique to cover each edge `{u,v}` that would disappear from
`C[ell]`. Updating the multiplicities immediately ensures that a later removal
cannot consume the final covering assignment for any such edge.

If an assignment `(v,C[ell])` is retained, some `u` in `C[ell]` has
`multiplicity[u]=1`, so `C[ell]` is then the unique clique covering `{u,v}`.
Later operations only remove assignments and cannot introduce another covering
clique. They also cannot remove `u` from `C[ell]`, because the same unique edge
would prevent that removal. Therefore every retained assignment still has an
uncovered-edge witness if removed from the final cover. The resulting cover is
assignment-minimal.

The greedy result is deterministic for fixed vertex IDs and input clique-label
order, but changing either order can produce a different assignment-minimal
cover.

## Time and space

Let the sizes below refer to a valid cover of the canonical graph entering
postprocessing. Its cliques are nontrivial, and every vertex occurs in at least
one clique because the canonical graph has no isolated vertices. A clique
`C[ell]` is scanned once for each of its vertex assignments while building
multiplicities and testing removals. Successful in-place erasures and their
multiplicity updates also have total cost quadratic in that clique's original
size. Initial sorting, the `O(n)` arrays, and moving the remaining cliques
together at the end fit within the
same bound. The total time is therefore

```text
O(sum_ell |C[ell]|^2).
```

If the graph has degeneracy `d`, every clique has at most `d+1` vertices. Hence

```text
sum_ell |C[ell]|^2
    <= (d+1) sum_ell |C[ell]|
    = (d+1) sigma(C).
```

On a nonempty canonical graph, `d` is at least one and every vertex belongs to
at least one clique, so `n <= sigma(C)`. The time is `O(d sigma(C))` and the
auxiliary space is `O(sigma(C))`, as in Lemma A.5. The membership lists contain
one entry per assignment, and the multiplicity and touched arrays contain at
most one entry per vertex. The constructions in this repository have
`sigma(C)=O(m)`, giving a postprocessing time of `O(dm)`.

## Construction-specific use

Postprocessing is timed and reported separately from construction. The
distinctions below concern the time for the complete construction followed by
postprocessing.

- **DPF:** DPF already returns an assignment-minimal cover by construction. Its
  proof is Lemma A.4. Enabling this stage therefore cannot reduce its cover and
  only repeats unnecessary work.
- **LZF and BCC:** Both constructions have `O~(m)` running time. Their
  assignment-minimal postprocessing can cost `O(dm)`, or `O(nm)` under the
  coarser maximum-clique-size bound, so their near-linear bound does not extend
  to a run with this stage enabled. This warning is especially relevant because
  their comparatively large covers might suggest substantial removable
  redundancy. On the evaluation graphs tested for this project, however, the
  reduction in total cover size from this stage was negligible for both
  constructions.
- **BPA, FPA, and CPA:** The postprocessing cost is absorbed by their analyzed
  running-time bounds. On some evaluation graphs, these constructions have also
  shown meaningful reductions in total cover size after assignment-minimal
  processing.

## Configuration and reporting

Enable the stage in a run specification with:

```toml
[postprocess]
assignment_minimal = true
```

Raw clique count and cover size are recorded before postprocessing. The JSON
report records the resulting clique count, cover size, and postprocessing time
separately. If CCF output is requested, the CCF contains the postprocessed
cover.
