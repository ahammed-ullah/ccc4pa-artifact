# wg_con

`wg_con` encodes the graph represented by a `cc_con` clique cover in WebGraph's
BV format. It measures a small set of compression settings and keeps the
smallest complete bundle for each graph. This prepares compressed inputs for
the later algorithm comparison; it does not run graph algorithms.

The only graph input is the final `.ccf` file. Each clique represents all
edges among its vertices. The encoder decodes the cover once, builds a compact
vertex-to-clique index, and gives WebGraph one sorted neighborhood at a time.
It merges duplicate neighbors from overlapping cliques within each row, so
each undirected edge appears in both directions. The CCF header supplies the
vertex and edge counts and the canonical graph fingerprint. The encoder keeps
the cover and vertex-to-clique index in memory and expands rows on demand.

## Compression search

The candidate list is near the top of `tune.py`. The first four trials use
the original vertex order at windows 16 and 32, then BFS+LLP order at windows
16 and 32. BFS+LLP runs breadth-first search on the graph recovered from CCF,
then applies layered label propagation to that BFS order. Later trials reuse
the saved order. If a failed or timed-out BFS+LLP trial leaves no saved order,
the tuner skips the next initial BFS+LLP trial. If either window-32 trial
succeeds, the tuner tries window 64 on the order with the smallest completed
window-32 bundle. If window 64 succeeds, it also tries window 128 on that
order. The paper's search uses at most six candidates per graph.
`INITIAL_CANDIDATES` and `EXTRA_CANDIDATES` in `tune.py` define the policy
recorded in the manifest.

The paper trials use WebGraph `BvComp`, unlimited reference depth, minimum
interval length 4, gamma coding for degrees, references, blocks, and
intervals, and zeta-3 coding for residuals. Run through `tune.py` to apply
these settings. If invoking `wg_encode` directly, pass `--references gamma`
along with the paper settings. The direct command defaults to unary reference
coding. A candidate dictionary may override `interval`, individual entries in `codes`,
`max_ref_count` (a nonnegative integer), or `bvgraphz` (`True` selects
`BvCompZ`). Give candidates with the same order and window distinct `id`
values. For example:

```python
{"id": "z_w32_r8", "order": "original", "window": 32,
 "max_ref_count": 8, "bvgraphz": True}
```

A window is the number of preceding adjacency lists WebGraph may consider as
references. When choosing the window-32 order and the final winner, the tuner
picks the smallest completed bundle and breaks size ties by alphabetical
candidate ID. It reports the selected WebGraph-to-CCF size ratio.

A runtime bundle contains `.graph`, `.properties`, and `.ef`. The `.ef` index
is needed for random neighbor access. A reordered bundle also contains a
`.perm` file mapping each new vertex ID to its canonical original ID; its
entries are little-endian `u32` values in new-ID order. Its bytes count toward
the bundle size. The `.offsets` file is used to build `.ef`
and is discarded. Each attempt's status, settings, encoding time, index time,
and file sizes are recorded in `manifest.json`.

Each graph has one directory under `format_wg`. During the search, its
`current/` slot holds the trial being measured and `best/` holds the smallest
completed bundle. A failed or larger trial is removed. After a successful
search, the selected files and `manifest.json` are moved to the graph
directory, and the working slots are removed. An interrupted run can resume
with the same inputs, binaries, and candidate policy. An existing completed
result is preserved while a changed policy is searched.

Each candidate has a six-hour limit, also bounded by the Slurm job's remaining
time with ten minutes reserved for finalizing. These limits are constants in
`tune.py`. A timed-out candidate contributes no size; the final result is the
smallest *completed* candidate. The manifest records skipped and timed-out
trials, so the chosen result is not presented as a global WebGraph minimum.

## Build

Install GNU Make, a Rust toolchain with Cargo (Rust 1.85 or newer), and Python
3.6 or newer. Rust is installed once in your account, not in each Slurm job.
On an RCAC cluster, first check `module spider rust`; if a suitable Rust
module exists, load it. Otherwise, install Rust and Cargo with Rustup:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- --profile minimal -y
source "$HOME/.cargo/env"
cargo --version
rustc --version
```

Build from `wg_con` on a machine that can download Rust crates. On Negishi,
use an interactive Slurm allocation for this substantial compilation:

```bash
make
make test
```

`make` builds `target/release/wg_encode` and installs the pinned WebGraph CLI
as `bin/webgraph`. It finds an existing Cargo in `$HOME/.cargo/bin` even if
that directory is not on `PATH`. `make clean` removes local build products;
run `make` again before submitting jobs after cleaning. Check the CCF files
once with `cc_check` before tuning: the encoder checks their structure but
does not repeat `cc_con`'s full checksum and graph verification for every
trial.

## One graph

```bash
python3 tune.py \
  --ccf /path/to/math.ccf \
  --output-dir /path/to/format_wg \
  --encoder ./target/release/wg_encode \
  --webgraph ./bin/webgraph
```

The selected files appear under `/path/to/format_wg/math/`, together with
`manifest.json`. The output directory is a CLI argument; the cluster script
below supplies a default.

## Negishi jobs

Run `make` after changing the Rust source. Changes to `tune.py` or the job
scripts do not need a rebuild. Individual jobs use the built
binaries and do not install Rust. `run.sh` reads CCF from
`$RCAC_SCRATCH/fall2026/format_ccf` and writes to
`$RCAC_SCRATCH/fall2026/format_wg`, unless `WG_OUTPUT_DIR` is set. Submit one
graph from `wg_con` with `sbatch run.sh math`, or the full graph list with
`./submit.sh`. The latter submits all 48 graph tasks without a concurrency
cap; Slurm starts as many as the available resources and account limits allow.
Set `WG_MAX_PARALLEL` to impose a cap when needed. Jobs request
eight CPUs and 64 GB by default; `WG_CPUS` and `WG_MEMORY` override those
requests for `submit.sh`. The same values may be overridden on a single job
with `sbatch --cpus-per-task=N --mem=SIZE run.sh GRAPH`.
`WG_ENCODER` and `WEBGRAPH_BIN` can name binaries outside this repository.

The extra CPUs speed up the LLP step when a BFS+LLP vertex order is first
computed. The tuner tries candidates one at a time, and BVGraph compression
uses WebGraph's sequential `comp_graph` call. The pinned WebGraph CLI builds
the `.ef` index by reading offsets in a sequential loop. Later candidates
reuse the saved order, so their compression times do not include another LLP
run.

If a job reaches its deadline during a candidate, its saved winner remains a
valid WebGraph bundle. After the first array finishes, continue just those
graphs with:

```bash
./submit_resume.sh
```

The script reads the manifests in `format_wg` and submits a Slurm array for
graphs with a timed-out or deadline-skipped candidate. The new jobs keep
successful trials and the current winner, then retry the unfinished trials.
Each continuation log starts with `run.sh: resuming GRAPH with --resume-incomplete`.
Run the same command again if a continuation also reaches its deadline.
`run.sh` sets the `csit` account, `cpu` partition, eight CPUs, 64 GB of memory,
and two-day wall time for each continuation. Negishi uses `normal` QoS when no
QoS is specified in that script.
`WG_MAX_PARALLEL` and `WG_OUTPUT_DIR` have the same meaning as in `submit.sh`.
`WG_RESUME_CANDIDATE_TIMEOUT_SECONDS` can raise the
per-candidate limit above its six-hour default if needed. Keep the same CCF
files, encoder binaries, and candidate list when continuing a search.

After the jobs finish, write the size summary, including missing graphs:

```bash
python3 summarize.py \
  --output-dir "$RCAC_SCRATCH/fall2026/format_wg" \
  > webgraph-sizes.csv
```

Each row gives CCF and WebGraph bytes, their difference and ratio, the
selected settings, and the number of successful and failed trials. The
selected bundles can later be used by `wg_algo`.

To export the recorded times for the selected bundles, run:

```bash
python3 summarize_times.py \
  --output-dir "$RCAC_SCRATCH/fall2026/format_wg" \
  > webgraph-selected-times.csv
```

This reads each final `manifest.json` and checks the bundle file sizes. It
reports `encode_seconds`, `index_seconds` for the `.ef` index, and their sum,
`selected_attempt_seconds`, for the winning candidate. Figure 23 uses this
sum. Each of the paper's winning trials started with its vertex order already
available and used single-threaded encoding and indexing.
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096)

The summary marks a missing manifest as `missing` and raises an error if a
manifest disagrees with its selected bundle.

To sum the recorded times of **all successful candidates** for each graph:

```bash
python3 summarize_search_times.py \
  --output-dir "$RCAC_SCRATCH/fall2026/format_wg" \
  > webgraph-successful-search-times.csv
```

This writes the number of successful trials and separate sums for encoding,
`.ef` indexing, and both. It reads `attempts` in each final `manifest.json`,
where each successful trial appears once. `successful_attempt_seconds`
includes any multithreaded BFS+LLP ordering done inside successful trials.
It is a lower bound on the full per-graph search time because it excludes
interrupted trials and work outside the timed subprocesses.
