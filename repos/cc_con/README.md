# cc_con

`cc_con` is a C++17 project for constructing clique covers of canonical simple,
undirected graphs. It accompanies
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).

The six construction names are:

- `bcc`: Balanced Cross-Cover
- `bpa`: Basic Phased Admissibility
- `cpa`: CrossCover Phased Admissibility
- `dpf`: Degeneracy Pivoted First-Fit
- `fpa`: First-fit Phased Admissibility
- `lzf`: first-fit variant of Lovasz's construction

LZF and BCC are the unaugmented constructions used as comparison baselines.
FPA and CPA are their phased-admissibility counterparts and return the concise
covers proved in the manuscript. BPA applies Local-Augmentation with phasing
directly to graph edges. DPF is the implementation name for DP-FirstFit from
Figure 15 and Lemma A.4.

The source keeps each construction's main loop concrete and independently
readable. It shares only small functions whose meaning is independent of an
algorithm's main loop, such as sorted-vector set operations and the exact
complement-coloring procedures used by more than one construction.

## Requirements

- a C++17 compiler;
- GNU Make;
- Python 3 for integration-test assertions.

Build and run the complete test suite with:

```bash
make
make test
```

`make` produces four public programs in `bin/`. The unit-test executables stay
under `build/`.

## Quick start

The construction CLI has one fixed form:

```text
cc_con <algorithm> --graph <graph.toml> --run <run.toml>
```

All six constructions can use the same small random intersection graph. The
graph specification generates the input locally.

```bash
example_graph=examples/graphs/hypergraph-k-uniform.toml
bin/cc_con bcc --graph "$example_graph" --run examples/runs/bcc.toml
bin/cc_con bpa --graph "$example_graph" --run examples/runs/bpa.toml
bin/cc_con cpa --graph "$example_graph" --run examples/runs/cpa.toml
bin/cc_con dpf --graph "$example_graph" --run examples/runs/dpf.toml
bin/cc_con fpa --graph "$example_graph" --run examples/runs/fpa.toml
bin/cc_con lzf --graph "$example_graph" --run examples/runs/lzf.toml
```

To try a graph from the paper, follow the Matrix Market instructions for
[coPapersCiteseer](examples/README.md#run-on-a-suitesparse-graph).

Every successful run writes `<algorithm>-<graph-name>.json` in the current
directory. A conflict selects `-2`, `-3`, and so on without replacing the
existing report. Names are reserved before construction, so repeated jobs for
the same algorithm and graph may run concurrently in that directory. Cover
output is optional and belongs in the run specification.

Use `bin/cc_con --list-algorithms` or
`bin/cc_con <algorithm> --help` to inspect the accepted names and parameters.
The [examples guide](examples/README.md) provides a runnable graph specification
for every supported source and synthetic distribution, plus an optional CCF
output run.

## Canonical graph preparation

`cg_prepare` converts any supported source into a reusable Canonical Graph
Format file:

```bash
bin/cg_prepare \
  --graph examples/graphs/hypergraph-k-uniform.toml \
  --output-dir prepared
```

For graph name `hypergraph-k-example`, this writes:

```text
prepared/hypergraph-k-example.cgf
prepared/hypergraph-k-example.cgf.json
prepared/hypergraph-k-example.cgf.toml
```

The generated TOML is accepted by `cc_con` through the same `--graph`
option. Preparing a large source once avoids repeating source parsing,
projection, canonicalization, and CGF encoding for every construction job.

Validate the prepared graph with `cc_check`.

```bash
bin/cc_check graph --graph prepared/hypergraph-k-example.cgf.toml
```

## Write and validate a cover

Use the prepared graph and the example run specification that enables CCF
output. Create its output directory before running the construction.

```bash
mkdir -p build/example-output
bin/cc_con bpa \
  --graph prepared/hypergraph-k-example.cgf.toml \
  --run examples/runs/write-cover.toml
```

`cc_check cover` checks the CCF structure and checksum. With `--graph`, it also
checks that every clique is valid and every edge of the graph is covered.

```bash
bin/cc_check cover --cover build/example-output/hypergraph-k-example.ccf
bin/cc_check cover \
  --cover build/example-output/hypergraph-k-example.ccf \
  --graph prepared/hypergraph-k-example.cgf.toml
```

## Lower bounds

`cc_bound` reads the `.cgf` file directly and computes lower bounds on the
minimum total size of a clique cover:

```bash
bin/cc_bound linear --graph prepared/hypergraph-k-example.cgf
bin/cc_bound local-mis --truss --graph prepared/hypergraph-k-example.cgf
bin/cc_bound local-color --truss --graph prepared/hypergraph-k-example.cgf
```

It does not accept graph TOML or construct a cover. The
[`cc_bound` guide](docs/cc-bound.md) lists all five supported runs.

## In-memory representations

Every construction receives a canonical
`std::vector<std::vector<int>> adj` with these guarantees:

- vertex IDs are dense in `0,...,n-1`;
- adjacency lists are sorted and duplicate-free;
- adjacency is symmetric;
- there are no loops or isolated vertices.

The construction writes `std::vector<std::vector<int>> C`, where `C[ell]` is
the sorted vertex list of output clique `ell`. Construction code uses no graph,
clique, cover, or generic-result wrapper classes. Raw-input and option validation
occur before the timed construction call. A construction run decodes prepared CGF
without repeating its full graph and checksum validation; `cc_check graph`
performs those checks explicitly.

In-memory vertex IDs and clique labels use `int`. The implementation assumes
that both `n` and `|C|` fit in that type. A workload outside this range requires
widening the corresponding types consistently across the project rather than
patching an individual construction or postprocessing routine. The 64-bit
counts in CGF and CCF do not change this in-memory assumption.

Canonical Graph Format (`.cgf`) stores the graph persistently. Clique Cover
Format (`.ccf`) is the only cover output format. Both are versioned,
checksummed binary formats in which one neighborhood or clique can be located
and decoded independently. Requesting CCF output always triggers semantic cover
verification before the file is written under its final name; verification
remains optional for statistics-only runs.

## Supported graph sources

- Matrix Market coordinate files;
- synthetic clique projections with k-uniform, Poisson, lognormal, or discrete
  power-law hyperedge sizes;
- Cornell temporal simplices;
- Cornell incidence-dual temporal simplices;
- Cornell comma-separated hyperedge lists;
- extracted Stack Exchange tag-question assignment files;
- prepared CGF files.

Every source produces the same canonical adjacency-list representation.
All reported `n`, `m`, cover statistics, and fingerprints refer to that final
graph.

## Repository map

- `src/main.cpp`: CLI parsing, algorithm selection, and timing boundaries.
- `src/algo_{bcc,bpa,cpa,dpf,fpa,lzf}.cpp` and the matching
  `include/cc_con/algo_*.hpp` files: complete constructions and their public
  function declarations.
- `src/first_fit.cpp`, `src/complement_degeneracy.cpp`: shared exact coloring
  procedures called inside construction timing.
- `include/cc_con/sorted_vector_set.hpp`: inline sorted-vector set mechanics.
- `src/graph.cpp`, `src/graph_*.cpp`: graph-format selection, preparation,
  and concrete readers or generators.
- `src/cgf.cpp`, `src/output.cpp`, `src/artifact_support.cpp`: CGF and CCF
  encoding, decoding, checksums, and completed-file renaming.
- `src/cg_prepare_main.cpp`, `src/cc_check_main.cpp`: the other two public
  graph-format programs.
- `src/cc_bound_main.cpp`, `src/bound*.cpp`: the direct-CGF lower-bound program,
  its independently readable procedures, and JSON reporting.
- `src/config.cpp`, `src/report.cpp`: strict TOML-subset parsing and JSON reports.
- `src/assignment_minimal.cpp`, `src/verification.cpp`: optional postprocessing
  and cover checks.
- `tests/test_main.cpp`: unit and randomized algorithm tests.
- `tests/bound_test.cpp`: exact small-graph checks for every lower-bound method.
- `tests/integration.sh`: public CLI, graph-source, CGF, CCF, and corruption
  checks run by `make test`.
- `examples/`: runnable graph and run specifications with small local input
  files covering every accepted source.

## Documentation

- [Construction guide](docs/constructions.md): common notation, manuscript
  correspondence, and the six-algorithm map.
- [Architecture](docs/architecture.md): program and file boundaries, timing, and how
  to add a construction or graph source.
- [Configuration](docs/configuration.md): accepted graph and run TOML keys.
- [Reporting](docs/reporting.md): JSON fields and timing interpretation.
- [`cc_bound`](docs/cc-bound.md): direct-CGF CLI, reports, and timing fields.
- [Lower bounds](docs/lower-bounds.md): individual bounds, their combination,
  proofs, complexity, and exact-truss storage.
- [Assignment-minimal postprocessing](docs/assignment-minimal.md): greedy
  procedure, correctness, implementation, and complexity.
- [Canonical formats](docs/output-formats.md): complete CGF and CCF contracts.
- [Synthetic hypergraphs](docs/hypergraph-generation.md): distributions,
  compatibility checks, and adaptive deduplication.
- [Cornell data](docs/cornell-data.md): required downloads and projection modes.
- [Stack Exchange data](docs/stack-exchange-tag-questions.md): extracted-file
  semantics and projection statistics.
- [Runnable examples](examples/README.md): every graph source, all six run
  specifications, CCF output, and prepared CGF input.

The six per-construction documents in `docs/` map their concrete vectors and
loops to the corresponding construction steps and explain implementation
choices that affect practical cost.
