# Hypergraph clique-projection generation

The synthetic `hypergraph` model samples hyperedges and projects each one to a
clique. It supports four hyperedge-size distributions:
`k-uniform`, `poisson`, `lognormal`, and `power-law`. Independently sampled
hyperedges are allowed to be identical; their projected graph edges are still
stored only once.

Small runnable specifications for all four distributions are under
[`examples/graphs/`](../examples/README.md#graph-specifications).

## Paper notation and configuration keys

The evaluation appendix and the TOML files use different names for a few
parameters. Their exact correspondence is:

| Paper | TOML key | Meaning |
| --- | --- | --- |
| $n_0$ | `n` | size of the sampling population before isolates are removed |
| $r$ | `r` | number of sampled sets |
| $c$ | `k` | fixed set size in the uniform model |
| $a$ | `mu` | mean of the normal variable underlying the lognormal model |
| $b$ | `sigma` | standard deviation of that normal variable |
| $\alpha$ | `alpha` | exponent of the discrete power law |
| $s_{\min}$ | `s_min` | minimum power-law set size |
| $s_{\max}$ | `s_max` | maximum power-law set size |

For the lognormal model, the implementation uses
$K_i=\lfloor e^{X_i}+1/2\rfloor$ for $X_i\sim\mathcal N(a,b^2)$, before
rejecting a value above $n_0$. This is the `round(exp(X))` operation described
below, not truncation by $\lfloor e^{X_i}\rfloor$.

## Common generation

For each of the requested `r` hyperedges, projection consists of:

1. Sample an accepted size `s` from the configured distribution.
2. If `s >= 2`, select a uniformly random `s`-subset of the requested vertices
   `0,...,n-1`; smaller sizes are recorded and skipped.
3. Emit all `s(s-1)/2` unordered pairs from the selected subset.
4. Append each pair to both endpoint adjacency lists, with deduplication handled
   by the selected strategy below.

A persistent permutation and partial Fisher-Yates swaps select the subset in
`O(s)` time without repeated trials for distinct vertices. Leaving the
permutation shuffled between hyperedges is valid: conditional on any current
permutation, the next partial shuffle still selects every `s`-subset uniformly.

The generator first samples the complete hyperedge sequence without enumerating
its pair sets. For a sampled size `s`, it adds `s-1` to the occurrence degree of
each selected vertex and adds `s(s-1)/2` to `P`. The occurrence degrees are exact
reserve sizes if all pair occurrences are appended during replay.

The RNG engine, every standard-library distribution object, and the permutation
are then reconstructed from their initial state. This exactly replays the first
pass, including rejected size samples. Reconstructing the distribution objects
matters because an implementation such as `normal_distribution` may cache random
state internally.

## Adaptive deduplication

After the first pass, let `N = n(n-1)/2`. Conditional on the sampled sizes, the
expected number of distinct edges is

```text
estimated_m = N * (1 - product_i (1 - s_i(s_i-1)/(n(n-1))))
rho = P / estimated_m.
```

The product is evaluated as a sum of logarithms for numerical stability. The
same fixed internal rule applies to every size distribution:

```text
rho <= 2  : reserve + append + sort/unique
rho >  2  : pre-sized flat hash
```

For `sort-unique`, each adjacency vector receives its exact occurrence capacity,
all projected pairs are appended, and each list is sorted and uniqued. At the
threshold this stores at most 16 bytes of adjacency integers per expected final
edge. That is below the hash path's adjacency plus hash storage and avoids all
hash probes. Lists that actually lose duplicates are rebuilt at their unique
size, so constructions do not retain their larger occurrence capacity.

For `hash`, a temporary flat open-addressed table stores canonical packed edge
keys. It is pre-sized from `estimated_m` with a 10 percent margin, capped by `P`,
so a normal run does not encounter an old-table-plus-new-table rehash peak. Only
the first occurrence of a key is appended to adjacency. Dynamic growth remains a
correctness fallback if the estimate is exceeded.

After either path, adjacency lists are sorted, and isolated vertices are removed
and densely renumbered in place. Thus the graph given to a construction remains
simple, undirected, isolate-free, and canonical.

The requested `n` is the sampling population, not necessarily the canonical
graph order. The report retains requested `n` under `graph.parameters.n` and
reports the order after isolate removal as `graph.n`. Original generated vertex
identifiers are intentionally not preserved.

## Size distributions

### K-uniform

Every size is exactly `k`, with `2 <= k <= n`.

### Poisson

Each trial uses `Poisson(lambda)`. Outcomes `0` and `1` are accepted sampled
sizes but contribute no vertices or graph edges. An outcome above `n` is rejected
and resampled. Consequently, accepted sizes follow the Poisson distribution
conditioned on being at most `n`; the lower tail is not clamped or resampled.

The implementation requires finite `0 < lambda <= n`. The upper bound keeps the
probability of accepting a size from becoming pathologically small.

### Lognormal

Each trial samples `X` from `Normal(mu,sigma^2)` and computes
`s = round(exp(X))`. Here `mu` and `sigma` parameterize the underlying normal
distribution; `mu` is not the mean of `exp(X)`. Sizes `0` and `1` contribute no
edges. A size above `n` is rejected and resampled, so accepted sizes are the
rounded lognormal distribution conditioned on being at most `n`.

Both parameters must be finite, `sigma > 0`, and
`mu <= log(n + 0.5)`. The bound places the median before the largest value that
rounds to `n`, keeping upper rejection practical.

### Discrete truncated power law

For `s_min <= s <= s_max`, the generator samples

```text
P(s) = s^(-alpha) / sum_[j=s_min..s_max] j^(-alpha).
```

It requires finite `alpha > 0` and
`2 <= s_min <= s_max <= n`. A discrete CDF is built once and searched for each
hyperedge; this is not a continuous Pareto sample.

Poisson and lognormal use at most 1000 attempts to obtain a size at most `n`.
Exceeding that limit stops generation with an error instead of leaving a job in
an unbounded rejection loop. All parameters, including unknown or inapplicable
keys, are validated before graph-sized storage or random generation begins.

## Time and space

Let

```text
P = sum_i s_i(s_i-1)/2
```

be the number of projected pair occurrences before deduplication, and let `m` be
the number of distinct graph edges. The first pass takes `O(sum_i s_i)` time and
the replay necessarily takes `O(P)` time. Final sorting uses the occurrence
degrees in the sort/unique path and the distinct degrees in the hash path. A very
large or heavy-tailed hyperedge can therefore make generation expensive even
when most of its pairs duplicate existing edges.

The sort/unique path uses `O(n+P)` space; its `rho <= 2` guard keeps expected
occurrence storage within twice the final edge storage. The hash path uses
`O(n+m)` space. Neither path constructs an incidence graph or a separate vector
containing all projected edge pairs.

## Generation statistics

Hypergraph reports include these values under `graph.generation_statistics`:

- `nontrivial_hyperedges`: accepted sizes at least two.
- `sampled_size_sum`, `sampled_size_min`, `sampled_size_max`, and
  `sampled_size_mean`: summaries over the `r` accepted sizes.
- `pair_occurrences`: `P`, including duplicate pair occurrences.
- `estimated_distinct_edges` and `estimated_pair_to_edge_ratio`: the first-pass
  values used to select a strategy.
- `deduplication_strategy`: either `sort-unique` or `hash`.
- `distinct_edges`: the exact final edge count, equal to canonical `graph.m`.

When `r=0`, all four size-summary values are reported as zero. Counters use signed
64-bit storage and generation stops with an error rather than overflowing them.

The seed initializes `std::mt19937`. Repeated runs using the same executable and
standard-library implementation are reproducible. C++ does not require its
Poisson, normal, or uniform distribution mappings to be bit-identical across
different standard-library implementations.
