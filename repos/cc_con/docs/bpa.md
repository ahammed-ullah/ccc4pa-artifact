# BPA Implementation

`src/algo_bpa.cpp` implements Basic Phased Admissibility. BPA applies
Local-Augmentation directly to uncovered graph edges, using the admissibility
relation and phase threshold described in Definitions 3.13 and 3.15 and the
discussion that follows in
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).

Unlike FPA and CPA, BPA does not begin with another clique-cover construction.
Each phase simply scans the graph edges. Common notation is defined in the
[Construction Guide](constructions.md).

## Stored sets

The paper uses `C`, `A`, and `D`. The implementation also stores `L` to
recognize covered edges:

- `C[ell]` is output clique `ell`.
- `L[v]` contains the labels of all output cliques containing `v`.
- `A[v]` contains the current-phase labels whose cliques may accept `v`.
- `D[j]` contains the vertices admissible to the `j`-th clique created in
  the current phase.

The labels in `C`, `L`, and `A` are global output labels. If
`first_label` is the number of cliques present when a phase begins, then
`D[ell-first_label]` belongs to global label `ell`. This offset means that
`D` stores entries only for the current phase.

All of these sets are sorted vectors without duplicate values. During a phase,

```text
ell is in A[v]  if and only if  v is in D[ell-first_label].
```

Consequently, `sigma(A)=sigma(D)`. The implementation maintains the current
value of `sigma(D)` and reports its largest value.

## One phase

A phase performs the following steps:

1. Start with empty `A` and `D`. Keep the cover `C` and its membership
   lists `L` from earlier phases.
2. Scan every edge `{u,v}` once, in increasing endpoint order.
3. Skip the edge if `L[u] intersect L[v]` is nonempty, because a common label
   identifies a clique already containing both endpoints.
4. Otherwise, choose a label from `A[u] intersect A[v]` if this intersection
   is nonempty, and augment that clique by `u` and `v`.
5. If no current label accepts the edge and the phase is still open, create the
   edge clique `{u,v}`.
6. If no current label accepts the edge and the phase is closed, leave the edge
   uncovered. Its presence causes another complete phase.

When a new edge clique `C[ell]={u,v}` is created, its admissible set is

```text
D[ell-first_label] = (N(u) intersect N(v)) union {u,v}
                   = N[u] intersect N[v].
```

When `u` and `v` augment an existing clique, the corresponding set becomes

```text
D[ell-first_label]
    = D[ell-first_label] intersect N[u] intersect N[v].
```

The code removes rejected vertices from `D` in place. Whenever a vertex `w`
leaves this set, it removes `ell` from `A[w]` at the same time, preserving
the relation above.

## Phase boundary

Immediately after creating a clique, BPA tests

```text
sigma(D) > tau*m.
```

The clique that causes the strict inequality remains active in the current
phase. From that point onward, the phase may still augment its existing
cliques, but it creates no new clique. If some uncovered edge cannot augment
one of those cliques, the next phase scans all graph edges again.

The source does not store the paper's set of currently uncovered edges. Instead,
`L[u] intersect L[v]` answers whether `{u,v}` is covered. This avoids copying
the graph and deleting edges as cliques grow, which is useful when the
construction completes in one phase. The cost in a later phase is that covered
edges are encountered again and skipped through `L`.

## Choosing a label

Definition 3.13 permits any label in the admissible-label intersection. The run
configuration makes this choice deterministic:

- `min-label`: choose the smallest common label.
- `max-label`: choose the largest common label.
- `min-clique`: choose a common label whose current clique is smallest.
- `max-clique`: choose a common label whose current clique is largest.

For the clique-size rules, a tie is resolved by the smallest label.

The source scans the two sorted `A` vectors directly. The label rules are
written in `algo_bpa.cpp` so that the reader can see exactly which label is
selected in the main edge loop. No temporary intersection vector is built.

The analysis charges an intersection by
`O~(min{|A[u]|,|A[v]|})`, which can be obtained by searching the smaller set
in the larger one when their sizes are very different. The implementation uses
a two-pointer scan, whose worst-case cost is
`O(|A[u]|+|A[v]|)`. This was deliberately retained because it was faster on
the evaluation graphs, including the 24-billion-edge graph: it reads contiguous
vector entries and often stops at an early common label. Thus this particular
operation does not directly realize the skew-aware intersection cost used in
the formal analysis.

Removing one label from an `A[v]` vector uses `lower_bound` followed by
`vector::erase`. This keeps `A` compact and was faster than the tested
alternative that retained inactive entries. It is also a practical
implementation choice rather than a claim about the cost of every individual
dynamic-set update.

## Parameters and statistics

`tau` defaults to `4.0` and must be at least one. The available
`label_selection` values are the four rules listed above.

`num_phases` is the number of complete graph-edge scans. A nonempty graph has
at least one BPA phase. `peak_size_D` is the largest current-phase value of
`sigma(D)`; it is reported because `sigma(D)` determines when a phase
closes. Generic report fields give the final number of cliques, total cover
size, and construction time.
