# Cornell/ARB Higher-Order Data

`cc_con` supports three concrete native formats from Austin Benson's
[Cornell/ARB data repository](https://www.cs.cornell.edu/~arb/data/). They are
independent readers, not a generic hypergraph-format layer:

- `cornell-simplices`: parallel simplex-size and member files;
- `cornell-dual-simplices`: the incidence dual of those same files;
- `cornell-hyperedges`: one comma-separated hyperedge per line.

Each reader removes repeated members within one hyperedge, globally
deduplicates projected graph edges, removes projection isolates, and assigns
dense canonical vertex IDs. The six cover constructions consequently receive
the same simple, undirected adjacency-list representation regardless of source.

## Temporal simplices

A temporal-simplex archive provides one simplex size per line in
`<dataset>-nverts.txt`, concatenated member IDs in
`<dataset>-simplices.txt`, and usually one timestamp per simplex in
`<dataset>-times.txt`. Static clique projection needs only the first two files.

```toml
[graph]
name = "email-Enron"
source = "file"
format = "cornell-simplices"
nverts_path = "/absolute/data/email-Enron-nverts.txt"
simplices_path = "/absolute/data/email-Enron-simplices.txt"
```

The reader reports native hyperedge counts, incidence sums, size statistics,
pair occurrences, distinct projected edges, native cover size, native
compression ratio, and projection overlap.

## Dual temporal-simplex projection

`cornell-dual-simplices` reverses the native incidence relation. Every input
simplex position becomes a tentative graph vertex. For each original entity
`v`, the reader constructs a dual hyperedge `D_v` containing all input
positions whose simplex contains `v`. Clique projection of these `D_v` sets
joins two events exactly when they share an original entity.

For example, thread records become graph vertices while each participating user
induces one clique:

```toml
[graph]
name = "math-sx-threads"
source = "file"
format = "cornell-dual-simplices"
nverts_path = "/absolute/data/threads-math-sx-nverts.txt"
simplices_path = "/absolute/data/threads-math-sx-simplices.txt"
```

Download the native archive containing `nverts` and `simplices`. A separate
Cornell projected-graph archive represents the projection on the original
entities, not this incidence dual, so it cannot validate this mode. Timestamps
and labels are not runtime inputs.

Input events retain distinct sequence IDs while incidence is reversed;
identical events are not merged. Events with no dual edge are removed during
canonicalization. Reports distinguish the original vertex and simplex
counts, dual hyperedge and incidence statistics, pair occurrences, isolated
dual vertices removed, and canonical dual `n` and `m`.

A runnable small input pair and graph specification are provided in
[`examples/`](../examples/README.md) for both the original-entity and incidence-dual
interpretations. A downloaded dataset uses the same two keys with its own
`nverts` and `simplices` paths.

## Direct hyperedge lists

Cornell labeled-hypergraph archives such as Amazon reviews and Stack Overflow
answers contain `hyperedges-<dataset>.txt`, with one comma-separated hyperedge
per line. Only that member is required; node labels, label names, and archive
README files are not runtime inputs.

```toml
[graph]
name = "amazon-reviews"
source = "file"
format = "cornell-hyperedges"
path = "/absolute/data/hyperedges-amazon-reviews.txt"
```

The reader accepts nonnegative integer IDs separated by commas, with optional
whitespace around IDs and delimiters. The runnable
[`cornell-hyperedges.toml`](../examples/graphs/cornell-hyperedges.toml)
specification uses a small bundled file with the same syntax.

## Running constructions

No construction option depends on the source format. For example:

```bash
bin/cc_con bcc --graph examples/graphs/cornell-dual-simplices.toml --run examples/runs/bcc.toml
bin/cc_con bpa --graph examples/graphs/cornell-dual-simplices.toml --run examples/runs/bpa.toml
bin/cc_con cpa --graph examples/graphs/cornell-dual-simplices.toml --run examples/runs/cpa.toml
bin/cc_con dpf --graph examples/graphs/cornell-dual-simplices.toml --run examples/runs/dpf.toml
bin/cc_con fpa --graph examples/graphs/cornell-dual-simplices.toml --run examples/runs/fpa.toml
bin/cc_con lzf --graph examples/graphs/cornell-dual-simplices.toml --run examples/runs/lzf.toml
```

Without `[output]`, each command writes only its JSON report in the current
directory. Add CCF output only to the run whose cover should be retained.

Each raw-source invocation repeats parsing and projection. That time is
reported separately as `graph.time_seconds`, so construction timings remain
comparable, but six jobs also perform six projection passes. Prepare CGF once
when source preparation is expensive:

```bash
bin/cg_prepare \
  --graph examples/graphs/cornell-dual-simplices.toml \
  --output-dir build/example-prepared

bin/cc_con bpa \
  --graph build/example-prepared/cornell-dual-simplices-example.cgf.toml \
  --run examples/runs/bpa.toml
```

Use the exact generated filename, which follows `graph.name`. CGF decoding
remains graph time, outside the selected construction call. Run
`cc_check graph --graph <generated.cgf.toml>` separately when full checksum,
canonical-graph, and fingerprint validation is required.

## Size and memory considerations

For native input, statistics and pair generation use each deduplicated
hyperedge. Singleton and empty hyperedges affect source statistics but generate
no graph edge; vertices isolated after projection are absent from canonical
`n`.

External IDs must fit the supported integer range. Gaps and one-based IDs are
allowed because surviving vertices are remapped. Readers stop on malformed
records, counter overflow, or a graph too large for the concrete representation;
they do not truncate large hyperedges.

Projection deduplicates unordered pairs immediately in a packed 64-bit edge
table, so its principal edge storage is proportional to distinct `m`, not total
pair occurrences `P`. Runtime is still proportional to `P`. One very large
native or dual hyperedge can therefore make projection expensive even when the
final graph is much smaller.

The final adjacency list contains `2m` integer entries, or `8m` bytes when
an `int` uses four bytes. Each of the `n` inner vectors also stores its
address, current size, and allocated capacity, and the memory allocator may add
more bookkeeping. These values, the output cover, and the construction's own
sets must all fit simultaneously. Preparing CGF separates source-projection
memory from later construction jobs, but a construction still fully decodes
the adjacency lists.
