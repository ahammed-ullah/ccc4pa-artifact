# Reporting

Every successful run writes one JSON report in the current directory. Its
preferred name is:

```text
<algorithm>-<graph-name>.json
```

For example, BPA on `email-enron` writes `bpa-email-enron.json`. If that file
already exists, the next reports are `bpa-email-enron-2.json`,
`bpa-email-enron-3.json`, and so on. Different algorithms naturally receive
different prefixes. Before construction begins, each process atomically creates
a temporary claim directory for one report name. Simultaneous repetitions of
the same algorithm and graph therefore receive distinct suffixes even when they
use the same working directory.

The report is first completed in a temporary file and then renamed. Therefore,
the final `.json` file is a visible completion marker: graph preparation,
construction, requested postprocessing, verification, and cover writing have all
succeeded. The claim is removed when the process exits normally or reports an
error. Standard output contains only a short completion line with the final
report path; errors go to standard error.

## JSON contents

- `schema_version`: version `1` of the run-report field layout. This is separate
  from CGF/CCF binary format versions.
- `algorithm`: construction name and construction parameters. BPA, FPA, and CPA
  record `tau` and `label_selection`; BCC, DPF, and LZF have empty parameter
  objects.
- `graph`: graph name, original source/type parameters, optional generation
  statistics, current and origin specification paths, input format, canonical
  graph SHA-256 fingerprint, canonical `n` and `m`, and graph preparation time.
  CGF input additionally records its CGF and preparation-report paths.
  Hypergraph generation
  statistics summarize accepted sizes, projected pair occurrences, estimated
  `P/m`, the selected deduplication strategy, and exact distinct edges. Matrix
  Market input has no source-specific generation statistics. Native Cornell input reports `r`,
  `r_nontrivial`, incidence sums, size statistics, `P`, `m`, native cover size,
  `native_CR`, and `overlap_factor`. A Cornell dual projection reports original entity/event counts, dual-hyperedge
  size and incidence statistics, `dual_pair_occurrences`, `n_dual`, `m_dual`,
  `dual_native_cover_size`, `dual_native_CR`, and `dual_overlap_factor`. A Stack
  Exchange tag-question projection reports source question/tag counts, tag
  degrees and incidence sums, `pair_occurrences`, `potential_CR`, exact distinct
  edges, native-cover statistics, `canonical_question_count`, and
  `isolated_questions_removed`.
- `constructed_cover`: raw number of cliques, total cover size, and construction
  time.
- `algorithm_statistics`: measurements specific to the selected construction.
  BPA and FPA report `num_phases` and `peak_size_D`. BCC reports `mu`,
  `splitting_applied`, `complement_degeneracy`,
  `num_complement_colors`, and `num_parts`; `part_size_limit` appears only
  when splitting was performed. CPA reports those BCC values,
  `bcc_num_cliques`, `bcc_cover_size`, `num_phases`, and
  `peak_size_D`. DPF reports the input graph's `degeneracy`. LZF currently
  reports an empty object.
- `postprocess`: whether assignment-minimal pruning ran and, when it did, the
  resulting clique count, cover size, and time.
- `output`: whether a cover was written and, for CCF, its format version, path,
  complete encoded bytes, file SHA-256 checksum, and combined streaming
  encoding/write time.
- `verification`: whether verification was explicitly requested, whether it was
  performed, its trigger, result, and time. The trigger is `explicit`,
  `cover-output`, or `explicit-and-cover-output`.

`cover_size` is `sum_ell |C[ell]|`, the number of vertex-to-clique assignments.
It is not an in-memory or file byte estimate.

A BPA or FPA report contains a section such as:

```json
{
  "algorithm_statistics": {
    "num_phases": 2,
    "peak_size_D": 746
  }
}
```

The meaning of `num_phases` follows each pseudocode's repeated work:

- BPA counts complete scans of the graph edges. A nonempty graph has at least
  one BPA phase.
- FPA counts passes containing at least one nonempty pair `(v,ell)`. It may
  report zero when its initial first-fit cliques require no such pass.
- CPA counts passes over CrossCover cliques. It reports zero when BCC produces
  no CrossCover clique.

For all three constructions, `peak_size_D` is the largest stored
`sigma(D)`. For FPA and CPA this includes the admissibility sets created for
their initial color classes, even when `num_phases` is zero.

CPA's `bcc_num_cliques` and `bcc_cover_size` describe the exact BCC family
used by the implementation: the original non-singleton complement-color
classes plus every CrossCover clique before CPA restricts it to `B_i'`. These
values are counted while the family is generated; CPA does not store the whole
family at once.

Configuration parsing, algorithm selection, cover statistics, postprocessing,
verification, encoding, writing, and reporting are excluded from
`constructed_cover.time_seconds`. Only the selected construction call is timed.
For CPA, that timed call includes BCC construction, initial color-class duals,
temporary-tree work, and owned-edge tests; for BCC, no CPA admissibility work is
performed.

Each report records one construction execution. `cc_con` does not repeat or
average construction calls; an experiment that requests repeated measurements
must launch independent commands and combine the resulting reports separately.
The included `submit.sh` schedules five such commands for each graph in
`graphs.txt`; `submit_highmem.sh` does the same for `graphs_large.txt`. Their
Slurm job and log names include the repetition number. Cover sizes and all
other algorithmic counts are deterministic for a fixed graph and run
specification.

For raw sources, graph reading, generation, clique projection, validation,
isolate removal, and renumbering the remaining vertices are included in
`graph.time_seconds`, not construction time. For prepared CGF, that field
contains decoding and parsing its preparation report;
full `cc_check` validation is a separate command and is not part of a
construction report. See [`cornell-data.md`](cornell-data.md) for the precise
interpretation of Cornell statistics.

When CCF output is requested, semantic verification is mandatory and occurs
before encoding. Statistics-only runs preserve the explicit optional setting.
The JSON report is still published last, so its presence means every requested
stage, including implied output verification and CCF publication, completed.

`cc_bound` has a separate report whose name starts with `bound-` and whose
contents are limited to graph identification, individual lower bounds, their
vertexwise combination, and phase timings. Its layout is documented in
[`cc-bound.md`](cc-bound.md); it does not change the construction-report schema
described above.
