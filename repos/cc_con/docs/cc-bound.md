# `cc_bound`

`cc_bound` computes lower bounds on the minimum total size of a clique cover,
denoted `sigma(C*)` in the paper.
It does not construct or read a clique cover. See
[`lower-bounds.md`](lower-bounds.md) for the definitions and proofs.

## Input and commands

The program accepts only a Canonical Graph Format file directly:

```bash
bin/cc_bound linear --graph prepared/graph.cgf
bin/cc_bound local-mis --graph prepared/graph.cgf
bin/cc_bound local-mis --truss --graph prepared/graph.cgf
bin/cc_bound local-color --graph prepared/graph.cgf
bin/cc_bound local-color --truss --graph prepared/graph.cgf
```

`--truss` may appear before or after `--graph`. It is rejected with `linear`.
Use `bin/cc_bound --list-methods` to print the three method names.

Graph TOML, Matrix Market, hypergraph sources, and generated CGF TOML are not
accepted. Use `cg_prepare` once to produce the `.cgf` file, then pass that file
directly to every bound run.

`cc_bound` decodes every CGF neighborhood. It checks the header, counts,
offsets, encoded record boundaries, and vertex ranges needed for safe decoding.
It does not recompute SHA-256 values or repeat symmetry and canonical-graph
validation. Run `cc_check graph` on the generated `.cgf.toml` when explicit
file and graph validation is required.

## Reports

A successful run writes JSON in the current directory. Preferred names are:

```text
bound-linear-graph.json
bound-local-mis-graph.json
bound-local-mis-truss-graph.json
bound-local-color-graph.json
bound-local-color-truss-graph.json
```

If the preferred name exists, `-2`, `-3`, and so on is inserted before `.json`.
The report is written to a temporary file and renamed only after completion.

The report uses schema version 2 and contains:

- `schema_version`: version of this JSON layout;
- `algorithm`: the method and whether exact truss was requested;
- `graph`: the CGF filename, path, `n`, `m`, encoded bytes, and decode time;
- `lower_bounds`: every individual lower bound computed by the selected run and
  `combined_best`;
- `timing_seconds`: times for the major shared procedures and the complete
  bound computation.

Only applicable individual fields appear. For example, `linear` reports
`global_mis`, `global_core_color`, and `combined_best`; a
`local-color --truss` run additionally reports both local MIS values, common
global color, exact truss, local color, and truss-filtered local color.

The timing fields describe procedures because one pass may contribute to
several mathematical bounds. For example, the exact-truss support
pass also obtains triangle counts used to recognize simplicial vertices.
`bound_computation` excludes CGF decoding; `total_including_graph_read` includes
it.

The report does not contain a cover size, compression ratio, optimality gap,
per-vertex arrays, or counts describing which lower bound was largest. Comparisons with
constructed covers can be performed later by matching graph names across JSON
reports.

Each report records one execution of the selected method. CGF decoding is
excluded from `bound_computation` and recorded separately, so repeated file-read
time is not part of the reported lower-bound computation time.
