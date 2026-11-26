# CPA Implementation

`src/algo_cpa.cpp` implements CrossCover-PA (Figure 4). Lemma 3.24 gives the
conciseness and resource bounds in
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).

CPA shares the BCC preparation and CrossCover procedures in `src/algo_bcc.cpp`.

The paper applies one phased-admissibility step to each BCC clique `B_i` using
a spanning tree `T_i`. The implementation separates the BCC family into its
two natural types:

- the complement-degeneracy classes `F[ell]`;
- the CrossCover cliques `B_i=R_i union Z_i`.

The first type initializes the cover and its admissibility relation directly.
Only the CrossCover cliques enter CPA's repeated processing loop. Common
notation is defined in the [Construction Guide](constructions.md), and the
shared starting family is described in [BCC](bcc.md).

## Initial color classes

CPA calls `prepare_bcc_cross_cover` inside its timed construction call. Every
non-singleton complement-degeneracy class `F[ell]` is inserted directly into
the output cover. These classes are pairwise nonmergeable: when a vertex
received a later complement color, a graph nonedge prevented it from joining
each earlier class.

For each initial output clique `C[q]`, CPA constructs

```text
D[q] = intersection_{v in C[q]} N[v]
```

and places `q` in `A[w]` for every `w` in `D[q]`. Thus the color classes
initialize the first phase exactly as the first-fit classes initialize FPA.
They do not need a temporary tree or a CrossCover-PA test.

The intersection stops when `|D[q]|=|C[q]|`. The clique itself is always
contained in its admissible set, so no later closed-neighborhood intersection
can make the set smaller than this value.

## Sets maintained by CPA

The source stores:

- `C[q]`: output clique `q`;
- `L[v]`: all output labels of cliques containing `v`;
- `A[v]`: current-phase labels whose cliques may accept `v`;
- `D[q-first_label]`: the admissible vertices of a current-phase label.

`C` and `L` remain across phases. `A` and `D` are discarded at a phase
boundary.

The paper uses the letter `L` inside Figure 4 for the temporary set of labels
that accept all of `B_i`. The source calls that temporary set
`admissible_labels`, because `L[v]` already denotes the persistent
clique-membership lists above. These are different objects.

As in BPA and FPA,

```text
For a graph edge {u,v}:
{u,v} is covered  if and only if  L[u] intersect L[v] is nonempty.
```

Each membership-list test scans comparable lists together or binary-searches
for labels from the shorter list in the longer one. Its cost depends on the
membership-list lengths. Lemma 3.24 accounts for coverage using explicit
uncovered-neighbor sets `M_v`.

## Restricting a CrossCover clique

A CrossCover clique has two sorted sides, `B_i=R_i union Z_i`, and owns
exactly the edges in `R_i x Z_i`. Before applying Figure 4's
phased-admissibility step, the source scans these owned edges. It retains every
endpoint incident to an owned edge that is still uncovered:

```text
R_i' = endpoints in R_i of uncovered edges in R_i x Z_i
Z_i' = endpoints in Z_i of uncovered edges in R_i x Z_i
B_i' = R_i' union Z_i'.
```

`B_i'` is implementation notation; the current Figure 4 states the step for
`B_i`. The reduction does not change the required work. Edges inside
`R_i` or inside `Z_i` are already covered by the initial complement-color
classes. Every edge in `R_i x Z_i` is assigned to this one CrossCover clique.
An omitted vertex is incident to no uncovered edge assigned to `B_i`, so
adding it to an output clique cannot be necessary for coverage. The remaining
set `B_i'` is a subset of the clique `B_i`.

If no owned edge is uncovered, the CrossCover clique is finished without an
admissibility test. If all vertices on both sides have already been marked, the scan stops
because the reduced set is known to equal the original set. If a vertex has no
cover labels, its complete row in the product is uncovered and can be marked
without testing every edge separately.

Only reduced `R_i'` and `Z_i'` are retained when a CrossCover clique must cross
a phase boundary.

## The tree and the paper's label intersection

Choose `x` in `R_i'` and `y` in `Z_i'`. The source constructs the
two-star edge set

```text
{{x,z} : z in Z_i'}
    union
{{y,r} : r in R_i' - {x}}.
```

This is a spanning tree of `B_i'` with `|B_i'|-1` edges. It is stored only
while the current `B_i'` is processed. Its edges belong to the original owned
product `R_i x Z_i`, so using this smaller tree preserves the edge charging
used in the paper.

Figure 4 computes the temporary set

```text
L = intersection_{{u,v} in E(T_i)} (A[u] intersect A[v]).
```

The source computes the same set in `admissible_labels`. It starts with the
tree edge having the smallest `min{|A[u]|,|A[v]|}`. It stops immediately if
an intersection becomes empty. Once only one candidate label remains, two
binary searches per remaining tree edge decide whether that label survives.
These choices only change the order in which the same intersections are
evaluated.

For intersections that must retain several labels, the shared sorted-vector
operation scans similarly sized vectors together and searches the smaller
vector in the larger one when their sizes are substantially different. No
hash-based set is used.

## The atomic update

If `admissible_labels` is nonempty, CPA chooses one label `q` and adds all
of `B_i'` to `C[q]` in one step. It then:

1. adds `q` to `L[v]` only for vertices newly added to `C[q]`;
2. intersects `D[q-first_label]` only with the closed neighborhoods of those
   new vertices;
3. removes `q` from `A[w]` for every `w` removed from that `D` set.

Sorted-vector insertion into `L[v]` and erasure from `A[w]` can shift later
labels in those lists.

If no label accepts `B_i'` and the phase is open, CPA creates
`C[q]=B_i'`, constructs its admissible set, and updates `A`. The
closed-neighborhood intersections begin with the tree edge having the smallest
adjacency-list estimate and stop when the admissible set contains only the
clique.

If no label accepts `B_i'` and the phase is closed, its reduced sides are
retained for the next phase. A closed phase may still augment labels already
created in that phase.

The selection rule determines which accepting label is used:

- `min-label`: smallest accepting label;
- `max-label`: largest accepting label;
- `min-clique`: label of a smallest accepting clique;
- `max-clique`: label of a largest accepting clique.

Clique-size ties select the smallest label.

## Phases and retained BCC data

A phase stops creating cliques after

```text
sigma(D) > tau*m.
```

The clique causing the strict overflow remains part of that phase. At the next
boundary, `A` and `D` are cleared, while `C` and `L` remain.

During the first phase, CPA constructs and processes one nonempty pair of
temporary BCC parts at a time. Once that pass ends, it releases the sorted
crossing-edge records and the part arrays. A later phase therefore stores only
the reduced CrossCover cliques that were actually deferred; it does not rebuild
or retain the complete BCC family.

## Bounds and statistics

Lemma 3.24 proves that the analyzed CrossCover-PA algorithm returns a concise
cover with

```text
sigma(C) = O(min{m,n*sqrt(mu)})
```

in `O~(d^2 min{m,mu})` time and `O(m)` space.

The reported time includes the membership-list tests and sorted-vector updates
described above. It also includes `std::sort` of `k` packed crossing-edge
records, which uses `O(k log k)` integer comparisons. The [BCC guide](bcc.md#recording-and-grouping-crossing-edges)
explains how sorting and grouping differ from the proof.

`tau` defaults to `4.0` and must be at least one. `num_phases` counts
passes over CrossCover cliques; it is zero if BCC produces no such clique.
`peak_size_D` is the largest value of `sigma(D)`, including the initial
color-class admissibility sets.

CPA also reports the BCC structural values described in [BCC](bcc.md).
`bcc_num_cliques` and `bcc_cover_size` describe the exact BCC family used by
this implementation: the non-singleton original color classes plus every
CrossCover clique as first generated, before the `B_i'` reduction. This
family is counted as it is generated and is never stored in full.

CPA construction time includes BCC preparation, initial admissibility sets,
CrossCover generation, uncovered-owned-edge scans, temporary trees, and all
phases.
