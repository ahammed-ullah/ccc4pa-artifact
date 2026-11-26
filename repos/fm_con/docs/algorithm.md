# FM Construction

## Graph Used By FM

Let the input graph be a canonical simple undirected graph `G` with `n`
vertices and `m` edges. Section 3 of the Feder-Motwani paper replaces each
vertex `v` by a left copy `v_L` and a right copy `v_R`. Every undirected edge
`{u,v}` gives two directed cross edges:

```text
u_L -> v_R
v_L -> u_R
```

The bipartite graph used by the partition construction therefore begins with
`m_hat = 2m` cross edges. The implementation reads the original adjacency
lists directly as these cross edges; it does not construct two explicit copies
of every vertex.

The general-graph representation also has one return arc `v_R -> v_L` for
each original vertex. These `n` arcs are counted in the report but need not be
constructed to compute the partition.

## Delta Choice

For any construction run with a fixed delta, the paper defines

```text
k_hat = floor(delta log2(n) / log2(2 n^2 / m_hat)).
```

If the command includes `--delta d`, the implementation requires `0<d<1` and
runs the complete construction once with `delta=d`. The value remains fixed
while `k_hat` is recomputed after each stripped biclique.

If `--delta` is absent, the program chooses candidates automatically. At the
start of every candidate run, `m_hat=2m`. Define

```text
a = log2(n) / log2(n^2 / m).
```

The positive attainable initial values of `k_hat` are the integers `j` with
`1 <= j < a`. The program tries one midpoint for each such integer:

```text
lower_j = j / a
upper_j = min((j + 1) / a, 1)
delta_j = (lower_j + upper_j) / 2.
```

Thus every tried value satisfies `0 < delta < 1`, and its initial `k_hat` is
`j`. The report records `a`, the first and last `j`, and the first and last
midpoints. The complete tried sequence follows from the formula above.
The implementation evaluates the logarithms, ratios, and midpoints in
`long double` and floors the defining ratio to obtain `k_hat`.

There are at most `O(log n)` attainable values of `j`, so automatic selection
performs at most `O(log n)` independent candidate runs. This samples one delta
from every interval producing a distinct positive initial `k_hat`. It does not
sample every potentially different execution: two deltas with the same initial
`k_hat` can cause `k_hat` to decrease at different times as edges are removed.

Every candidate run starts from the original CGF graph. After every stripped
biclique, `k_hat` is recomputed from the number of cross edges that remain
while `delta` stays fixed. The selected run minimizes, in order:

1. total biclique-partition size;
2. `m_star`;
3. number of center vertices;
4. `delta`.

This is the best of the stated automatic candidates, not a claim that the
chosen `delta` is globally optimal.

If no positive initial `k_hat` is attainable, every cross edge is reported as
a one-edge residual part without building the FM search structures.
A fixed run starting at `k_hat=0` gives the same residual-only partition and
records the requested delta. Both paths use the graph counts in the CGF header.

## Clique Stripping For k_hat At Least Two

For each left vertex `u`, the paper's neighborhood tree stores `d_(u,w)`, the
number of neighbors in the right-vertex range represented by node `w`. Both
workspace methods below compute these same counts and use the same FM scoring
and tie rules.

At stage `t`, the binary search fixes the next right vertex. For each branch it
computes the conditional score from Lemma 2.1:

```text
c_b = sum over u in U_t of
      d_(u,w.b) * (d_u - 1) falling-factorial (k_hat - t).
```

The scores can exceed 64 bits even when the final reported counts do not. A
small nonnegative integer type carries out score multiplication, addition,
and comparison exactly.

The paper presents equal binary ranges after assuming that `n` is a power of
two. This implementation pads the leaf range to the next power of two and
counts only real, unselected right vertices as eligible in each branch. If
one branch has no eligible vertex, the search takes the other. Otherwise it
compares the two scores divided by their eligible counts using exact cross
multiplication. Ties select the lower-ID branch.

The pseudocode removes each selected right vertex from its data structure
during one Clique Stripping call. Both implementations leave the state
unchanged during that search and subtract the already selected vertices from
the relevant counts. Every vertex still in `U_t` is adjacent to all those
selected vertices, so the resulting `d` values are exactly the same. Once both
sides `U_K` and `K` are known, the implementation removes exactly the edges in
`U_K x K`. This avoids temporary updates that would later need to be restored
for left vertices that leave `U_t`.

## Default Linear Workspace

The default `--workspace linear` method stores each sorted CGF adjacency row
in one packed array. If vertex IDs need `b=ceil(log2 n)` bits, every directed
cross edge uses exactly `b` bits for its neighbor ID. Row offsets use `O(n)`
64-bit entries.

The mutable state of one construction run has three parts.

- one deletion bit per directed cross edge
- one 32-bit Fenwick entry per block of up to 256 consecutive positions in a
  row, plus the unused index-zero entry for each row
- current left and right degrees and `O(n)` search arrays

To obtain `d_(u,w.0)` and `d_(u,w.1)`, the algorithm binary-searches the
packed row at the right-vertex midpoint. The deletion bits and Fenwick entries
then count the active edges on each side. The block size of 256 keeps the
rank index to about one bit per eight cross edges plus `O(n)` overhead for
partial blocks and unused entries. A boundary scan examines at most 256
deletion bits.

If any candidate starts at `k_hat>=2`, the program builds the immutable packed
adjacency once and shares it across the candidates that use it. Each such run
initializes the deletion bits and Fenwick counts to zero and sets both degree
arrays to the original CGF row cardinalities. The peak workspace is

```text
O(n + m_hat)
```

where `m_hat=2m` is the number of directed cross edges. More concretely, its
dominant storage is approximately

```text
m_hat * (ceil(log2 n) + 1 + 32/256) bits.
```

The input has no isolated vertices, so `n <= 2m`; consequently this
`O(n+m_hat)` workspace is `O(m)` for every accepted graph.

Using the stored `n` and `m` for `math`, a conservative capacity estimate is
about 120 GiB including the linear arrays, instead of more than 1 TiB for even
the minimum prefix-tree allocation. This is an allocation estimate; the full
graph run is needed to measure its actual peak and running time.

The cost of the smaller workspace is an additional logarithmic factor for a
dynamic range count. A conservative bound for one Clique Stripping call is
`O(n k_hat^2 log^2 n)`, and the complete partition is bounded by
`O(m_hat n^delta log^3 n)`. The FM choices and the resulting partition do not
change. This is the bound for one fixed delta. Automatic selection costs
the sum of this bound over all tried deltas.

## Optional Prefix-Tree Workspace

`--workspace prefix-tree` retains the original implementation of the paper's
neighborhood trees. It stores every nonempty binary prefix explicitly; zero
branches are absent, but unary paths are not compressed. The CGF adjacency
lists are sorted, so one pass first counts the exact allocation and another
builds it.

This method has worst-case workspace

```text
O(n + m_hat log n)
```

Under the same input contract, this is `O(m log n)`, and it retains the paper's
`O(m_hat n^delta log^2 n)` partition-time bound for one fixed delta. Its
branch counts are direct node reads, but its explicit prefixes can exceed the
available memory on the largest evaluation graphs. It is never selected
automatically; it exists only when the command explicitly requests it.

## The k_hat Equal To One Case

For `k_hat=1`, Lemma 2.1 reduces to

```text
c_b = sum over u of d_(u,w.b).
```

This is the sum of current right degrees in branch `b`. One aggregate degree
tree makes the same conditional-average choices as all `n` neighborhood
trees. Each branch's average uses the count of real right vertices in that
range, including vertices of degree zero. The search compares averages by
cross multiplication and takes the lower-ID branch on a tie. The chosen part
is the star formed by that right vertex and all of its remaining neighbors.
Removing the star sets the chosen right degree to zero.

A run starting at `k_hat=1` reads the CGF row cardinalities as right degrees
and skips the neighbor IDs. When a run reaches `k_hat=1` after stripping larger
bicliques, it uses the current right degrees and releases the explicit trees
or mutable linear index before building the aggregate tree. Since `k_hat`
is nonincreasing, the aggregate tree suffices for the remaining searches.

When `k_hat=0`, all remaining cross edges become one-edge residual parts. This
also handles the interval in which the displayed stopping threshold in
Algorithm Partition would ask for a right side of size zero; the prose before
the algorithm gives the same `k_hat=0` stopping condition.

## Reported Sizes

Let:

- `P` be the sum of `|U_i|+|K_i|` over nontrivial stripped bicliques;
- `r` be the number of residual directed cross edges;
- `q` be the number of nontrivial bicliques, hence center vertices.

The report uses these identities:

```text
total_size  = P + 2r = sigma(B)
m_star      = P + r + n
n_star      = 2n + q
num_parts   = q + r
```

Here `m_star` counts `P` arcs incident to centers, `r` direct residual cross
arcs, and `n` return arcs. Singleton residual edges do not introduce centers.
