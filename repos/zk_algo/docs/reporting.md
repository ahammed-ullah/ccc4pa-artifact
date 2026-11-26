# Reporting and timing

Every successful invocation writes one JSON report in the current directory.
The first available name is, for example, `matching-math.json`; later or
concurrent runs use `matching-math-2.json`, `matching-math-3.json`, and so on.
A claim reserves the name. The final JSON appears only after success.

The report identifies the algorithm and matching variant, input path, `n`,
undirected edges `m`, and directed arcs `2m`. Under `input`, `graph_bytes`
means `.zk`, `index_bytes` means `.zki`, and `perm_bytes` means `.perm` (zero
when absent). `bundle_bytes` is their sum. The result is one scalar:
`matching_size`, `connected_components`, `selected_weight_sum`, or
`matched_vertex_weight_sum`; both weighted sums are exact decimal strings.
For MVM, `parameters` records epsilon, sample limit, sampling seed, and weight
source. `mvm_execution` records completed samples, positive augmentations,
and `complete` or `time_limit_partial` status. Its `stop_reason` is
`sample_limit`, `all_vertices_matched`, or `slurm_warning`. The second reason
means the matching covers every vertex and is optimal, even if fewer than the
planned samples were needed. The report also records config provenance and
the Slurm job ID when present.

`timing_seconds` separates these phases:

1. `zuckerli_load`: read `.zk` and `.zki`, check the required metadata, and
   prepare the decoder. Record the size of `.perm` if it exists.
2. `weight_preparation`, for WVC and MVM: generate or read weights and apply
   `.perm` when present.
3. `algorithm`: run the chosen algorithm on the loaded representation.

The main runtime comparison uses `algorithm`. It includes complete row
decoding, repeated reference resolution, matching endpoint queries, DFS
pending-child updates, and other algorithm auxiliary work. It excludes
report writing. Load time is retained for runs where shared-filesystem
access can be controlled; simultaneous jobs can disturb that measurement.

Immediately after the algorithm timer, the program reads
`getrusage(RUSAGE_SELF)` for `memory.process_peak_rss_bytes`. This is the
process's high-water resident memory since startup, including input loading
and WVC or MVM preparation. It is not an algorithm-only peak and uses the same
definition as `cp_algo` and `wg_algo`. Reading the counter is outside the
algorithm timer. Linux supplies the kernel count in KiB; macOS supplies
bytes.

Each Slurm job runs one invocation. Five independent jobs per graph and
algorithm yield five separately named reports; neither the executable nor
the submit script averages them. For MVM, the five jobs use sampling seeds
`1, ..., 5`, while generated vertex weights keep seed `19` in every job.

MVM's signal handler only sets a flag. The algorithm checks it between
samples and after scanning the first row for an already matched sampled
vertex. A stop at the second check leaves that sample uncounted. The
report records the matching reached through completed samples and is
published through the same reserved report name. A hard kill during a
single row decode can prevent a report.

For the paper, take the median of the five `algorithm` times and the median
of the five process peak RSS measurements for each graph and algorithm. All
five MVM runs on `math` and all five on `stackoverflow-answers` stopped near
the Slurm warning, about ten minutes before the 64-hour job limit. The paper
uses their observed time and peak RSS as lower bounds for a completed run
and marks these cases as partial.
