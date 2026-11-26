# DPF Implementation

`src/algo_dpf.cpp` implements Degeneracy Pivoted First-Fit (DPF). A degeneracy
order chooses the pivots, and local first fit partitions each pivot's
still-uncovered earlier neighbors. Each class forms a new output clique with
the pivot. Figure 15 calls this construction DP-FirstFit in
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).

Lemma A.4 proves assignment-minimality and gives `O(dm)` time and `O(m)` space
for the paper's algorithm.

## Construction

Let `pi` be a degeneracy order in which every vertex has at most `d` earlier
neighbors. DPF processes pivots from the end of `pi` to its beginning. For
`v=pi[i]`, it performs these steps:

1. Form `X` from the earlier neighbors of `v` whose edge to `v` is still
   uncovered.
2. First-fit color the complement of `G[X]`. A current class accepts `u`
   exactly when `u` is adjacent in `G` to every vertex already in that
   class.
3. For every resulting class `Q[ell]`, add
   `Q[ell] union {v}` to the output cover.
4. Mark the edges inside `Q[ell]` as covered. The edges from `v` to
   `Q[ell]` need not be marked because `v` will not be a pivot again.

`X` is obtained by filtering the sorted adjacency list `N(v)`, so its
vertices remain in canonical vertex-ID order. This canonical order, not their
order in `pi`, is the local first-fit order. Every `Q[ell]` is consequently
already sorted. The pivot is inserted at its sorted position, and the completed
clique is moved directly to the output without a final sort.

## Degeneracy order

The helper `degeneracy_order` uses the standard array-bucket peel. During the
peel:

- `degree[v]` is the current degree of `v`;
- `pi[i]` is the vertex currently at position `i`;
- `pos[v]` is the current position of `v` in `pi`;
- `bin[k]` is the first unprocessed position in degree bucket `k`.

The vertices are first placed in degree order by counting sort. When peeling
`v` decreases the degree of an unprocessed neighbor `u`, the code swaps
`u` to the front of its current degree bucket, advances that bucket boundary,
and decreases `degree[u]`. This computes the peel in `O(n+m)` time without
a priority queue. Reversing the peel gives the order `pi` used by DPF.

The largest degree encountered when a vertex is peeled is the graph degeneracy
reported as `algorithm_statistics.degeneracy`.

## Covered-edge bits

`adj_begin[v]` is the position at which `adj[v]` begins if all adjacency
lists are viewed consecutively. DPF allocates one bit for every directed
adjacency entry. For an undirected edge `{u,v}`, only the bit belonging to the
endpoint later in `pi` is authoritative; the symmetric bit is unused.

There are `2m` directed adjacency entries, so this bit array occupies
`2m` bits, or `m/4` bytes, in addition to the `n+1` values in
`adj_begin`. Its positions use `size_t` because `2m` can exceed the range
of `int`; vertex IDs, local ranks, and clique labels remain `int`.

At pivot `v=pi[i]`, every earlier neighbor's authoritative bit is in
`adj[v]`, so the initial scan of `N(v)` both finds the candidate edge and
tests whether it is covered.

## Local first fit and coverage updates

For the current `u=X[j]`, first fit queries the graph's sorted adjacency
lists for neighbors among `X[0],...,X[j-1]`. It can search for each candidate
in the adjacency list that owns the covered bit, or scan `N(u)` once and use
`rank[w]` to recognize earlier members of `X`. These queries supply the
induced subgraph's adjacencies as needed.

Searching a short candidate prefix can skip most of a long adjacency list.
Scanning the list can save work when the estimated cost of the individual
searches is higher. If `a=deg(u)` and `h=ceil(log2(a+1))`, the code searches
when `j*h<a` and scans otherwise. It precomputes `h` for each list size.

For each earlier neighbor found, the code increments the count for that
neighbor's local color. A class `Q[ell]` accepts `u` exactly when this count
equals `|Q[ell]|`. The smallest accepting label is selected. Only color
classes encountered among the neighbors need to be checked.

The candidate-search branch records the authoritative covered-bit position
for every edge it finds. The scan branch records that position when `u` comes
later in `pi`. For each member of the selected class that comes later than
`u` in `pi`, the scan branch searches that member's adjacency list to locate
the bit. DPF sets each bit as soon as it selects the class, so local coloring
also completes coverage marking. The arrays `rank`, `color`, `count`, and
`seen` are reused across pivots.

## Bounds and output

The ordering and pivot scans take `O(n+m)` time. The paper's `O(dm)` bound
charges `O(|X|^2)` work for each local coloring. In the implementation,
finding candidate edges and locating the remaining covered bits can involve
logarithmic adjacency searches. The local work is bounded by
`O(|X|^2 log(1+Delta))`, where `Delta` is the maximum graph degree. Since
`|X|<=d` and every member of `X` corresponds to a distinct uncovered pivot
edge, the implementation takes `O~(dm)` time and `O(n+m)` space.

Every emitted local class contains at least one uncovered pivot edge. DPF
therefore emits at most `m` cliques and has at most `2m` vertex-to-clique
assignments.

DPF returns an assignment-minimal cover by construction, as proved in
Lemma A.4. Running the optional assignment-minimal postprocessing cannot reduce
a DPF cover and only adds work.

DPF has no run parameters. Its report adds `degeneracy` to the generic clique
count, cover size, and construction time.
