# BCC Implementation

`src/algo_bcc.cpp` implements Balanced Cross-Cover (BCC), using CrossCover
from Definition 3.20 and BCC from Definition 3.21 of
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).
Lemma 3.22 gives the cover-size bounds.

The same source also contains the two BCC steps used by CPA. Both constructions
therefore use exactly the same complement coloring, temporary parts, and
CrossCover cliques. Common notation is defined in the
[Construction Guide](constructions.md).

## Complement-degeneracy classes

BCC first computes a degeneracy coloring of the complement. Its color classes

```text
F[0], F[1], ..., F[p-1]
```

are cliques in the input graph. The implementation computes this coloring
without constructing the complement. If `s` vertices remain during the peel,

```text
degree_complement(v) = s - 1 - degree_G(v).
```

Thus a minimum-degree peel in the complement is a maximum-degree peel in the
input graph. `src/complement_degeneracy.cpp` maintains the input-graph degrees
in integer degree buckets, reverses the resulting peel order, and greedily
colors the complement in that order.

To test whether a color class accepts a vertex `v`, the code counts neighbors
of `v` already assigned to each class. The class accepts `v` exactly when
that count equals the class size. Every completed class is sorted by canonical
vertex ID.

The non-singleton classes `F[ell]` are placed directly in the BCC output.
Singletons cover no edge, but they still participate in the temporary parts
used by CrossCover.

## Temporary parts

Definition 3.21 splits the complement-color classes using the target part size
`n / sqrt(mu)`. When the implementation splits a class, it rounds that size up
to an integer.

```text
mu = binom(n,2) - m + 1
b  = ceil(n / sqrt(mu))
```

The resulting parts have at most `b` vertices and are used only to define the
CrossCover calls. The original `F[ell]` serves as the initial output clique
even when the class is split. It covers all edges between parts of the same
color class and contributes `|F[ell]|` assignments.

The source avoids splitting when

```text
m < 10^6 * n * sqrt(mu).
```

In that case, each `F[ell]` is one temporary part. This is an implementation
choice allowed by the bound, not a separate case in the paper's construction.
The CrossCover cliques own disjoint nonempty edge sets, and each such clique
has at most two vertex assignments per owned edge. Hence their total assignment
is at most `2m`. Under the condition above, this contribution is already
within the required `O(n sqrt(mu))` term for the fixed constant.

When the condition is false, the source computes `b` with integer arithmetic
so rounding near an integer cannot shift a part boundary. CrossCover processes
pairs from different original color classes because `F[ell]` already covers
the edges between parts of the same class.

The temporary parts are stored consecutively:

```text
part p = part_vertices[part_begin[p] ... part_begin[p+1]).
```

`part_of[v]` and `position_in_part[v]` are used only while crossing edges
are recorded.

## CrossCover

For two temporary parts, CrossCover chooses the smaller part as the target
side `Y` and the other as the source side `X`. Equal sizes use a fixed
part order so that results are deterministic. It groups source vertices by
their identical, nonempty neighborhoods in `Y`.

For one group,

```text
R = source vertices with the same neighborhood in Y
Z = that common neighborhood.
```

The clique is `R union Z`, and `R x Z` is its owned edge set. Across all
part pairs, these owned sets partition the graph edges between different
complement-color classes.

## Recording and grouping crossing edges

The source scans the adjacency lists once to count inter-color edges and once
to record them. It stores one 64-bit integer for each such edge. That integer
encodes, in order:

1. the pair of temporary parts;
2. the source vertex's position in its part;
3. the target vertex's position in its part.

Sorting these integers numerically therefore places all records for one part
pair together and, within that pair, places each source vertex's target
neighbors together. The global crossing-edge array uses eight bytes per
inter-color edge. No hash table is used.

Within one part pair, each source neighborhood is already a consecutive sorted
range. The code first groups likely-equal ranges by their length and two
64-bit fingerprints. These fingerprints are only a way to avoid comparing
obviously different ranges. Equality is always decided by comparing the actual
target positions. If equal fingerprints happen to describe different
neighborhoods, that small group is sorted by the complete neighborhood and
then separated exactly. A fingerprint collision therefore cannot change the
cover.

The proof of Lemma 3.24 accounts for sorting and grouping in linear time using
deterministic string sorting. The implementation uses `std::sort` on packed
records and uses fingerprints to limit full neighborhood comparisons. The
numeric ordering keeps the sort on contiguous records and avoids looking up
endpoint or part information during comparisons. Both BCC and CPA use this
implementation.

The resulting `R` and `Z` sets are sorted. Groups are returned in increasing
order of their smallest source vertex, so fingerprint values do not determine
the output order.

## Shared BCC steps

`prepare_bcc_cross_cover` performs:

1. complement-degeneracy coloring;
2. the decision whether temporary splitting is needed;
3. construction and sorting of the 64-bit crossing-edge records.

`construct_next_cross_cover_pair` reads the next nonempty part pair and
constructs its `R,Z` cliques. Direct BCC merges each `R` and `Z` into one
sorted output clique immediately. CPA keeps the sides separate because it must
identify the endpoints of uncovered edges in `R x Z`.

Only one part pair's CrossCover cliques are present at a time. The complete
CrossCover family is not stored in addition to the final cover.

## Bounds and statistics

Lemma 3.22 gives

```text
|C| = O(min{m,mu})
sigma(C) = O(min{m,n*sqrt(mu)}).
```

BCC itself is not guaranteed to be composition-minimal. FPA and CPA add phased
admissibility to related starting families to obtain concise covers.

The BCC report contains `mu`, the complement degeneracy, the number of
complement colors, the number of temporary parts, and whether splitting was
performed. `part_size_limit` is reported only when splitting was performed.
Construction time includes coloring, the part-size decision, optional
splitting, both scans of the adjacency lists, sorting the crossing-edge
records, exact CrossCover grouping, and merging the output cliques.

**Assignment-minimal warning.** BCC has `O~(m)` construction time.
Assignment-minimal postprocessing can take `O(dm)`, or `O(nm)` under the
coarser clique-size bound, so enabling it removes the near-linear bound for the
complete run. Although BCC's larger cover may appear likely to contain many
removable assignments, the evaluated graphs showed negligible reduction in
total cover size. This postprocessing should normally remain disabled for BCC.
