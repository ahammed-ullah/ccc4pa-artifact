# Reporting and timing

Every successful invocation writes one JSON report in the current directory.
For example, `matching-math.json` is used if available; later or concurrent
runs use `matching-math-2.json`, `matching-math-3.json`, and so on. A claim
directory reserves the name before timing. The final JSON appears only after
the run succeeds.
Both decoder modes use the same filename pattern. Store reports from separate
measurement series in separate folders. The `parameters.decoder` field records
which decoder produced each report, independently of the filename.

The report records the algorithm and variant, graph path, `n`, undirected
edge count `m`, directed arc count `2m`, sizes of `.graph`, `.properties`,
`.ef`, and `.perm`, and their total. It also records the config path, optional
Slurm job ID, and one scalar result: `matching_size`,
`connected_components`, `selected_weight_sum`, or
`matched_vertex_weight_sum`. WVC and MVM sums are exact decimal strings.
`parameters.decoder` is `bulk` or `iterator`, including when the default is
used without a config file. The default is `bulk`. Group repetitions by this
field before taking a median. Older iterator reports without this field
should be treated as iterator reports. All runs used for the paper report
`bulk` explicitly.

The clock records separate phases:

1. `webgraph_load`: read `.graph`, `.properties`, and `.ef`, and check the
   input metadata needed for decoding. Also record the size of `.perm` if it
   exists.
2. `weight_preparation` for WVC and MVM: generate or read canonical weights and
   reorder them if `.perm` exists.
3. `algorithm`: run the chosen algorithm on the loaded representation.

Only phase 3 is the main runtime comparison. It includes all neighbor
decoding, reference resolution, repeated random queries, DFS pending-child
updates, and algorithm auxiliary construction. It does not include report
writing.
The load time remains in the report for experiments where filesystem access
can be controlled.

Immediately after phase 3, the program reads `getrusage(RUSAGE_SELF)` to
report `process_peak_rss_bytes`. This is the process's high-water resident
memory since startup, including input loading and WVC or MVM preparation. It is
the same definition used by `cp_algo`; it is not a separate algorithm-only
peak. Reading the counter happens after the algorithm timer. On Linux the
kernel value is in KiB; on macOS it is already in bytes.

Each Slurm job runs one command. `submit.sh` requests five independent jobs
per graph, following the `cp_algo` arrangement. Independent JSON names and
Slurm job IDs identify repetitions.

For MVM, `parameters` also records `mvm_epsilon`, `mvm_seed`, and
`mvm_sample_limit`. The `mvm_execution` object records `samples_completed`,
`positive_augmentations`, `status`, and `stop_reason`. A `complete` run stopped
at `sample_limit` or because `all_vertices_matched`, which can use fewer
samples. `time_limit_partial` has stop reason `slurm_warning`. A partial result
is the current matching weight. Its time and peak memory are the observations
up to that point and serve as lower bounds for a completed run. The
fixed-sample approximation bound applies after the sample limit. A run that
matched every vertex is already optimal. The report is published after the
algorithm returns.

For the paper, take the median of the five observations per graph and
algorithm. Each MVM seed has one selected report. The five MVM runs on
`math` and the five on `stackoverflow-answers` stopped at the Slurm warning
about ten minutes before the 60-hour job limit. Their observed time and
memory enter the comparison with the partial results marked.
