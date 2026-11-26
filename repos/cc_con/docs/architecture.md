# Architecture

The project separates the steps of an experiment into source files, but keeps each
construction internally direct. This is a readability and verification choice,
not an attempt to eliminate all duplicated code.

## Construction interface

Every construction receives the same canonical
`std::vector<std::vector<int>> adj` and writes its cover directly into
`std::vector<std::vector<int>> C`. A construction may fill a small,
algorithm-specific statistics structure. There is no `Graph`, `AdjacencyList`,
`Clique`, `CliqueCover`, generic construction result, state-holding algorithm
class, or common algorithm registry between `main` and the construction.

Vertex IDs and clique labels are represented by `int`; the implementation assumes
both `n` and `|C|` fit in that type. Supporting a larger range requires widening
these representations consistently across construction and postprocessing
state. The binary formats use 64-bit counts independently of this in-memory
assumption.

Graph loading or generation in `src/graph.cpp` performs raw-input validation.
It guarantees dense `int` vertex ids, simple undirected edges, no
isolates, and sorted unique adjacency lists. `cg_prepare` stores exactly that
validated representation. Normal CGF construction runs do not repeat the full
validation; `cc_check` can repeat it explicitly. Construction options are
validated while their run specification is parsed. Timed construction functions
use these guarantees and do not repeat graph or size checks in their loops.

`src/main.cpp` contains an explicit branch for each algorithm. Adding a
construction requires:

1. Add `include/cc_con/algo_<name>.hpp` with its options, statistics, and direct
   function declaration. Omit types the algorithm does not need.
2. Add `src/algo_<name>.cpp` with the complete algorithm. The `algo_` prefix
   groups complete constructions separately from shared mechanisms.
3. Add one parse/call branch in `src/main.cpp` and place any measurements in
   `algorithm_statistics`.
4. Add focused tests, a run specification, and algorithm documentation.

High-level logic may be duplicated when constructions are related. This keeps
their construction-step correspondence, data structures, and performance choices
independently inspectable. One construction cannot change state later read by
another construction, and one algorithm does not call another through an
indirect interface.

## Shared construction mechanisms

Code is shared when a function receives everything it uses through its
arguments, stores nothing between calls, and does not decide the order of an
algorithm's main steps:

- `include/cc_con/sorted_vector_set.hpp` contains concrete inline operations on
  sorted `std::vector<int>` sets. These direct set operations are used inside
  the construction loops; they do not decide an algorithm's control flow.
- `src/first_fit.cpp` computes the fixed canonical-id-order first-fit complement
  coloring and returns the concrete `h` and `F` vectors. It is a direct
  subroutine called from inside each construction that uses it.
- `src/complement_degeneracy.cpp` computes the fixed complement degeneracy
  coloring used by BCC. It peels maximum-degree vertices from the input graph
  with degree buckets represented by integer arrays, then greedily colors the
  complement without constructing complement adjacency lists.
- `prepare_bcc_cross_cover` and `construct_next_cross_cover_pair` in
  `src/algo_bcc.cpp` define the exact shared BCC stages. Preparation returns the
  original complement-degeneracy classes, one sorted 64-bit value per crossing
  edge, and arrays describing the temporary parts. The pair routine produces
  explicit `R` and `Z` sides for one nonempty part pair. Direct BCC and CPA
  therefore use an identical family without storing that entire family at once.

LZF keeps restricted-set construction, clique creation, and iteration order in
`algo_lzf.cpp`. BPA calls the shared sorted-vector operations without moving any
of its algorithm loop out of `algo_bpa.cpp`. FPA keeps its per-vertex
restricted-set bucketing, cover-membership lists, admissibility structures,
augmentation, and phase logic in `algo_fpa.cpp`, while calling the same
first-fit routine. Shared preprocessing is never moved into `main`, so each
algorithm's construction time continues to include all of its required work.

DPF is intentionally independent of the shared canonical-order first-fit
routine. The degeneracy order determines the pivot order and the earlier
neighbors eligible at each pivot. Within that restricted set, the current code
processes vertices in canonical vertex-ID order. Packed bits over the original
adjacency hold covered-edge state. The bucket peel, local coloring, and coverage
updates therefore remain together in `src/algo_dpf.cpp`.

BCC's construction function prepares the shared family, moves the nontrivial color
classes into its cover, and generates and merges one part pair's `R,Z` cliques
at a time. CPA uses the same preparation and pair routine, initializes from the
same color classes, and processes only generated CrossCover cliques. It constructs
one temporary unrooted two-star edge set after reducing the current clique to the
endpoints of its still-uncovered CrossCover edges. It does not retain all of the
paper's trees at once. CPA releases BCC's ranked records and part map after the
first pass, before processing any deferred phase.

## Graph sources

Graph models have variable parameters, so those parameters live in a standalone
graph specification under `[graph.parameters]`. `src/graph.cpp` selects a
reader or generator from the source and model/format names. A new model gets its
own source file and one visible selection branch.

Every source returns the same adjacency-list type. File input that may contain
loops, duplicates, asymmetric entries, or isolated vertices receives full
canonicalization: loops and duplicates are removed, edges are made
symmetric, isolates are discarded, and dense ids are assigned. Synthetic sources
construct simple symmetric sorted adjacency directly, so their shorter cleanup
only removes isolates and remaps ids. Both paths establish the same concrete
guarantees before an algorithm is selected. The small `GraphMetadata`
structure belongs only to reporting and is never passed to a construction.

`prepare_graph(path, adj, metadata)` is the function used by `cc_con`. For a raw
source it performs source-specific work, canonical validation, and logical graph
fingerprinting. For a CGF produced by `cg_prepare`, it decodes every
neighborhood after checking the counts and offsets needed to read the file
safely. It does not hash the CGF or its preparation report, validate symmetry,
or recompute the logical fingerprint.

`prepare_and_validate_graph(path, adj, metadata)` is the function used by the
explicit checker. Its CGF path verifies both SHA-256 values, decodes all neighborhoods,
validates the canonical graph, recomputes its fingerprint, and requires all
duplicated counts, hashes, names, and paths to agree. `cc_check` uses this path;
`cg_prepare` also uses it if asked to re-encode an existing CGF. Raw sources have
the same validated preparation behavior through either function.

`src/graph_hypergraph.cpp` applies the same generation procedure to all four
hyperedge-size distributions. A first pass samples every hyperedge and obtains
the exact number of pair occurrences at each vertex, which also permits an
estimate of `P/m`. Replaying the same random sequence then either appends into
adjacency vectors with exactly reserved capacity and removes duplicates after
sorting, or rejects duplicate edges immediately in a pre-sized table. No
incidence graph or separate list of projected pairs is constructed.

`src/graph_cornell_dual_simplices.cpp` is a separate two-stage real-data path.
It reads events in sequence, builds concrete `D_v` vectors, then projects those
vectors through a global set of encoded unordered vertex pairs. Each `D_v` allocation is
released after its pairs are consumed, and the complete incidence dual is
released before symmetric adjacency is allocated. Only the final canonical
adjacency list is passed to a construction.

## Programs

Graph preparation and clique-cover construction are in one public repository,
but they are separate executables and modules:

- `cg_prepare` converts any supported source to CGF and writes its metadata and
  reusable TOML graph specification.
- `cc_con` either prepares a raw source in memory or reads CGF, then calls one
  construction directly.
- `cc_check` validates CGF, validates CCF structure without a graph, or performs
  full CCF clique/coverage verification against any supported graph specification.
- `cc_bound` accepts a `.cgf` file directly and computes lower bounds on
  minimum clique-cover total size. It has no raw-source, graph-TOML, run-TOML,
  construction, cover-input, or cover-output path.

The public execution paths are:

```text
raw graph TOML ----> prepare_graph ----> canonical adjacency lists
       |                                      |
       |                                      +----> cc_con + run TOML
       |                                                  |
       |                                                  +----> JSON report
       |                                                  +----> optional CCF
       |
       +----> cg_prepare ----> CGF + metadata JSON + generated CGF TOML
                                      |
generated CGF TOML ----> prepare_graph (CGF decode) ----> cc_con
                  |
                  +----> prepare_and_validate_graph ----> cc_check

CCF -------------------------------> cc_check structure validation
CCF + any graph TOML --------------> cc_check semantic cover validation

CGF file ----> read_cgf ----> cc_bound ----> bound JSON report
```

`cc_bound` is also separated at link time. Its `bound*.cpp` files are linked
into `bin/cc_bound` but excluded from `bin/cc_con`, `bin/cg_prepare`, and
`bin/cc_check`. Conversely, no construction object is linked into `cc_bound`.
The new program therefore cannot add a branch, condition, or data structure to
an existing construction or graph-preparation loop.

CCF is the interface between this repository and downstream tools. `cp_algo`
uses its own reader to decode a complete cover or retrieve cliques one at a
time, depending on the selected mode. `wg_con` and `zk_con` use CCF covers to
recover graph neighborhoods for encoding. Use `cc_check cover` with the graph
specification to verify a cover and its edge coverage before handing it to a
downstream tool.

## Run order and timing

`src/main.cpp` performs these stages:

1. Parse the graph specification and run specification independently.
2. Load, canonicalize, and validate a raw graph, or decode a prepared CGF
   without repeating full validation; record graph time.
3. Atomically reserve a unique JSON report name in the current directory.
4. Call BCC, BPA, CPA, DPF, FPA, or LZF directly; record construction time.
5. Compute raw cover statistics.
6. Optionally modify `C` in place to make it
   [assignment-minimal](assignment-minimal.md).
7. Verify the final `C` when explicitly requested or whenever CCF output is
   requested.
8. Optionally stream the verified final `C` to `<graph-name>.ccf`.
9. Write the JSON report and publish it under the reserved name.

Only step 4 is included in an algorithm's construction time. The algorithm is
selected once before its function is called; that selection adds no work inside
an edge or set-operation loop.

The report name is reserved by atomically creating a temporary claim directory.
This gives simultaneous repetitions distinct numeric suffixes. The report is
published last by renaming a completed temporary file. Thus a final report
records a fully successful run, while an interrupted run does not leave a
partial JSON document under the final name.

CGF and CCF are also written through temporary files and renamed only after the
complete header, offset table, encoded records, and checksum have been finalized.
