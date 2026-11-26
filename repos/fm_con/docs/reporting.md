# JSON Report

Every successful command writes one JSON object using report schema version 4.
Only the selected run has full partition and timing statistics; candidate runs
that were not selected are not listed individually.

`algorithm.delta_selection` is `automatic` when `--delta` is absent and
`fixed` when one value was supplied. Automatic reports are named
`fm-graph.json`; fixed-delta reports include the original argument, as in
`fm-graph-delta-0.6.json`. Before construction begins, each process atomically
creates a temporary claim directory for one report name. Simultaneous runs with
the same graph and delta therefore receive distinct numeric suffixes. The final
JSON file appears only after the complete report has been written.

The `algorithm.workspace` field is `linear` for the default `O(n+m)` method or
`prefix-tree` for the optional explicit-prefix method. Because accepted graphs
have no isolated vertices, these bounds are respectively `O(m)` and
`O(m log n)`. Every fixed or candidate run uses the requested workspace.

## Graph

- `name`: CGF filename without `.cgf`.
- `path`: absolute input path.
- `format_version`: accepted CGF version.
- `file_bytes`: physical CGF file size.
- `canonical_fingerprint_sha256`: fingerprint stored in the CGF header.
- `file_checksum_sha256`: file checksum stored in the CGF header.
- `n`: number of original vertices.
- `m`: number of original undirected edges.

The SHA-256 strings are read from the header. They are not recomputed during
the FM run.

## Delta Selection

- `policy`: `distinct-initial-k-midpoints-v1` for automatic selection or
  `fixed-delta` for one requested run.
- `scale_a`: value `a` used to derive all candidates.
- `initial_k_min`, `initial_k_max`: range of tried initial `k_hat` values.
- `candidate_count`: number of complete FM candidate runs; this is one for a
  fixed delta, including when its initial `k_hat` is zero.
- `candidate_delta_min`, `candidate_delta_max`: first and last tried values,
  or `null`.
- `selected_delta`: delta of the selected run, or `null`.
- `selected_initial_k`: initial `k_hat` of the selected run.
- `time_seconds`: time for the input index, all candidate runs, and any
  comparison among them.

For a fixed delta, both candidate endpoints and `selected_delta` equal the
requested value. When automatic selection has `candidate_count` equal to zero,
the direct residual result is used and the three delta fields are `null`.

## Partition

- `initial_cross_edges`: `2m`, the edges of the directed bipartite expansion.
- `k_ge_2_bicliques`: parts stripped while `k_hat >= 2`.
- `star_bicliques`: parts stripped while `k_hat = 1`.
- `num_parts`: nontrivial bicliques plus residual one-edge parts.
- `edges_in_k_ge_2_bicliques`: cross edges assigned to those bicliques.
- `edges_in_star_bicliques`: cross edges assigned to stars.
- `residual_edges`: remaining cross edges, denoted `r`.
- `nontrivial_size`: sum of `|U_i|+|K_i|` over `k_hat >= 2` and star bicliques,
  denoted `P`.
- `residual_size`: `2r`.
- `total_size`: `P+2r`, equal to `sigma(B)` and compared with clique-cover
  total size `sigma(C)`.

All edge, part, and size counts are unsigned 64-bit integers.

## FM Representation

- `m_star`: `P+r+n`, comprising center arcs, residual cross arcs, and return
  arcs.
- `n_star`: `2n+q`, comprising left/right copies and center vertices.
- `center_count`: `q`, one center for each nontrivial biclique.
- `return_arcs`: `n`, one from each right copy to its left copy.

These are counts from the paper's representation. The program does not build
or write that representation.

## Construction State And Timing

- `space_bound` states the workspace bound of the requested method.
- `packed_neighbor_bits` is the fixed width of one packed neighbor ID.
- `packed_neighbor_words` is the immutable packed adjacency allocation.
- `deleted_edge_words` is the one-bit-per-edge deletion allocation.
- `deletion_rank_entries` is the number of 32-bit blocked Fenwick entries.
- `neighborhood_tree_nodes` is the explicit prefix count for the optional
  prefix-tree method.

Allocation counts that do not apply to the selected workspace are zero. All
allocation counts in `construction_state` are zero when no candidate starts
with `k_hat >= 2`.

`timing.selected_run_seconds` measures the selected candidate from its
workspace initialization through partition accounting. The initialization,
`k_ge_2_stripping_seconds`, and `star_stripping_seconds` fields record its main
phases. `timing.input_index_seconds` separately measures the one-time build of
the immutable packed adjacency for the linear method when a candidate starts
with `k_hat >= 2`. `delta_search.time_seconds` includes the index build, every
candidate run, and the comparisons used to select a result.

Each fixed delta is executed once. The program does not repeat runs or average
timings; repeated experimental measurements, if desired, must be combined from
separate reports. The root `submit.sh`, `submit_long.sh`, and
`submit_highmem.sh` scripts schedule five independent fixed-delta runs for the
four-hour, 24-hour, and high-memory lists, respectively. The separate
`graph_delta` files and `submit_delta` scripts preserve the delta-selection
trials rather than timing repetitions.

## Paper Metrics

Section 5.2 uses the median of five `timing.selected_run_seconds` values per
graph. The reports use a fixed selected delta and the default linear
workspace. The median measures candidate execution, with the one-time packed
input index and delta selection outside that timing. The compression ratios
in Figures 11 and 21 use `2 * graph.m / partition.total_size`.
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096)
