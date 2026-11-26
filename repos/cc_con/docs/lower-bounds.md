# Lower Bounds on Assignment-Optimal Cover Size

`cc_bound` computes lower bounds on the total size of an assignment-optimal
clique cover. Let

$$
\mathcal C^*=(C_1,\ldots,C_p)
$$

be such a cover, and let

$$
L_v=\{\ell:v\in C_\ell\}.
$$

The objective used in the paper is

$$
\sigma(\mathcal C^*)
  =\sum_{\ell=1}^p |C_\ell|
  =\sum_{v\in V}|L_v|.
$$

The procedures below use the canonical simple, undirected, isolate-free graph
stored in CGF to obtain lower bounds on each $|L_v|$ and sum them. Appendix C.2 of
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096)
gives two representative bounds used in the evaluation. This document describes
all bounds implemented by `cc_bound`.

## Independent-set bounds

Let $I_v$ be an independent set in the open-neighborhood graph $G[N(v)]$.
Every edge from $v$ to a vertex of $I_v$ must be covered, while a clique
containing $v$ can contain at most one vertex of $I_v$. Therefore

$$
|L_v|\geq |I_v|.
$$

The same conclusion holds for an independent set in $G[N[v]]$. If the set does
not contain $v$, the preceding argument applies. If it contains $v$, it contains
no neighbor of $v$ and has size one; the canonical graph has no isolated
vertices, so $v$ belongs to at least one cover clique.

### Global MIS

The `linear` method constructs one greedy maximal independent set $S$ in
canonical vertex order and uses $S\cap N[v]$ at every vertex. The resulting
bound is

$$
\sum_{v\in V}|S\cap N[v]|
  =\sum_{s\in S}(|N(s)|+1).
$$

The implementation increments the value for $s$ and all its neighbors when
$s$ enters the MIS, so it never constructs the individual intersections. The
JSON field is `global_mis`.

### Local MIS

For every vertex $v$, this procedure independently constructs a greedy maximal
independent set $I_v$ in $G[N(v)]$. It considers the vertices of $N(v)$ in
canonical order. When $u$ is selected, the procedure excludes
$N(u)\cap N(v)$ from subsequent selection. This is the representative
vertex-based bound described in Appendix C.2. The JSON field is `local_mis`.

### Simplicial-dominating local MIS

A vertex $s$ is simplicial when $N[s]$ is a clique. Adjacent simplicial
vertices have identical closed neighborhoods. The implementation chooses one
representative from each distinct simplicial closed-neighborhood class by
scanning simplicial vertices and marking their simplicial neighbors.
Representatives of different classes are nonadjacent.

Let $S$ be the set of representatives. For each $v$, the independent set
$S\cap N[v]$ is used as a fixed seed and then greedily extended to a maximal
independent set $I_v^{\mathrm{simp}}$ of $G[N[v]]$. Consequently,

$$
\sum_{v\in V}|I_v^{\mathrm{simp}}|
  \geq \sum_{v\in V}|S\cap N[v]|
  =\sum_{s\in S}|N[s]|.
$$

This dominates the bound obtained from the simplicial classes alone. It need
not dominate the independently constructed local MIS at every vertex, so both
lower bounds are retained. The JSON field is
`simplicial_dominating_local_mis`.

A vertex is simplicial exactly when the number of triangles containing it is
$\binom{|N(v)|}{2}$. Triangle counts are obtained during the
common-neighborhood or truss-support pass, so no additional triangle
representation is stored.

## Edge-based bounds

Suppose $Q(uv)$ is an upper bound on the order of every clique containing edge
$uv$. Assign each edge incident on $v$ to one cover clique that contains it. A
clique of order $q$ receives $q-1$ such edges, and each assigned edge contributes
at most $1/(q-1)$. Therefore

$$
|L_v|\geq
\left\lceil
  \sum_{u\in N(v)}\frac{1}{Q(uv)-1}
\right\rceil.
$$

Several procedures produce valid values of $Q(uv)$. A smaller upper bound on
clique order gives a larger lower bound on $|L_v|$.

### Global core and color

Let $\kappa(v)$ be the core number of $v$, and let $\phi$ be the proper global
coloring obtained by first fit in reverse degeneracy-removal order. For every
color represented in $N(v)$, retain the largest value

$$
\min\{\kappa(v),\kappa(u)\}
$$

over neighbors $u$ with that color. For nonnegative values
$a_1,\ldots,a_k$, define their threshold number by

$$
h(a_1,\ldots,a_k)
  =\max\{r:|\{i:a_i\geq r\}|\geq r\}.
$$

Let $p(v)$ be this threshold number for the retained color values. The other
$q-1$ vertices of a $q$-clique containing $v$ have distinct colors and core
numbers at least $q-1$. Hence $q-1\leq p(v)$, and

$$
Q_{\mathrm{global}}(uv)=1+\min\{p(u),p(v)\}.
$$

Appendix C.2 presents the simpler relaxation that uses only the number of
colors appearing in each neighborhood. The implementation also uses the core
numbers as above; this can decrease $Q_{\mathrm{global}}(uv)$ and therefore
strengthen the bound, while retaining linear time. Core decomposition,
coloring, and all neighborhood threshold calculations take
$O(n+m)=O(m)$ time because the graph has no isolated vertices. The JSON field
is `global_core_color`.

### Global colors in a common neighborhood

For edge $uv$, let $X=N(u)\cap N(v)$. For every global color represented in
$X$, retain

$$
\max_{\substack{w\in X\\\phi(w)=c}}
  \left(\min\{\kappa(u),\kappa(v),\kappa(w)\}-1\right).
$$

If the threshold number of these values is $h_{uv}$, then
$Q_{\mathrm{common}}(uv)=h_{uv}+2$. The $q-2$ other vertices in a clique
containing $uv$ have distinct colors and core number at least $q-1$. The JSON
field is `common_neighborhood_global_color`.

### Exact truss

The trussness $\tau(uv)$ is the largest $k$ for which edge $uv$ belongs to a
$k$-truss. Edges outside every triangle have trussness two, and every edge of a
$q$-clique has trussness at least $q$. Therefore

$$
Q_{\mathrm{truss}}(uv)=\tau(uv).
$$

The implementation computes initial triangle supports and performs exact
array-bucket support peeling. It stores one 32-bit support or final truss value
per undirected edge. Edge order and inverse position use 64-bit values because
the evaluated graphs may have more than $2^{32}$ edges. No hash table or edge
identifier for every directed adjacency entry is stored.

The peak truss workspace is approximately $20m+O(n)$ bytes beyond the decoded
adjacency lists: four bytes per edge for support or trussness and two eight-byte
arrays for bucket order and inverse position. Finding the identifiers of the
other two edges in a triangle uses binary search in sorted adjacency lists. The
JSON field is `exact_truss`.

### Local coloring of a common neighborhood

For each edge $uv$, first fit colors $G[N(u)\cap N(v)]$ in canonical vertex
order. Applying the same core threshold to these local color classes yields
$Q_{\mathrm{local}}(uv)$. The common neighborhood, its colors, and its scratch
arrays are released or reused before the next edge; the procedure never stores
all common-neighborhood graphs. The JSON field is
`common_neighborhood_local_color`.

When exact trussness is available, every local color class additionally retains

$$
\max_w\left(\min\{\tau(uw),\tau(vw)\}-2\right).
$$

The threshold number of these values plus two gives
$Q_{\mathrm{truss\text{-}local}}(uv)$. In a $q$-clique containing $uv$, the
$q-2$ other vertices have distinct local colors and both incident edges have
trussness at least $q$. The JSON field is
`truss_filtered_local_color`.

## Combining the bounds

Each independent-set method and each edge method is summed over all vertices
and reported separately. To combine the edge methods, the implementation first
uses the smallest available clique-order upper bound on every edge:

$$
Q_*(uv)=\min_j Q_j(uv).
$$

It then takes the strongest available vertex bound:

$$
B(v)=\max\left\{
  |I_v^1|,\ldots,|I_v^k|,
  \left\lceil
    \sum_{u\in N(v)}\frac{1}{Q_*(uv)-1}
  \right\rceil
\right\}.
$$

The reported `combined_best` is $\sum_v B(v)$. It can exceed the maximum
individual reported total because the strongest edge-order bound can differ by
edge and the strongest vertex bound can differ by vertex.

## Conservative reciprocal arithmetic

Floating-point addition can round a reciprocal sum upward and report an integer
that has not been proved. The implementation instead chooses a power of two
$R$ such that $\Delta R\leq 2^{63}$, replaces every $1/k$ by
$\lfloor R/k\rfloor/R$, and accumulates the integer numerators in 64 bits. The
final ceiling is therefore always a valid lower bound.

This conservative value can be one smaller than the exact reciprocal formula
when the exact sum lies extremely close to an integer. The total discarded
fraction at a vertex is less than $|N(v)|/R<1/2$; for the evaluated graphs it is
much smaller.

## Supported runs

| Command | Lower bounds computed | Time | Extra space excluding exact truss |
|---|---|---:|---:|
| `linear` | global MIS, global core/color | $O(m)$ | $O(n)$ |
| `local-mis` | linear bounds, both local MIS bounds, common global color | $\widetilde O(dm)$ | $O(n+m)$ |
| `local-mis --truss` | preceding bounds and exact truss | $\widetilde O(dm)$ | $O(n+m)$ |
| `local-color` | local-MIS bounds and local color | $\widetilde O(d^2m)$ | $O(n+m)$ |
| `local-color --truss` | all bounds and truss-filtered local color | $\widetilde O(d^2m)$ | $O(n+m)$ |

The logarithmic factors hidden by $\widetilde O$ come from membership and
edge-identifier searches in sorted vectors. Intersections use the project's
hybrid merge/binary-search routine. The local MIS and local coloring procedures
also choose between scanning an adjacency list and searching the smaller local
set. These choices change implementation cost but not the mathematical lower
bounds.
