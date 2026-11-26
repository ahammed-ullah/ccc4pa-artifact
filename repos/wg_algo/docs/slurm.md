# Negishi runs

Build the C++ program once in `wg_algo`:

```bash
make
```

`run.sh` reads selected WebGraph bundles from
`$RCAC_SCRATCH/fall2026/format_wg/GRAPH/GRAPH.graph`. Submit one graph from
the `wg_algo` directory:

```bash
sbatch run.sh matching math
```

`submit.sh` reads the 48 graph names in `graphs.txt` and starts five
independent jobs per graph. With no config, these use the bulk decoder:

```bash
./submit.sh matching
./submit.sh bfs
./submit.sh dfs
./submit.sh wvc
./submit.sh mvm
```

To use maximal matching, run `./submit.sh matching config/maximal.toml`.
For WVC file weights, use a config containing a `weights_file` entry.
To run the earlier iterator decoder, use
`./submit.sh bfs config/iterator.toml` (and likewise for the other algorithms).
A custom config can combine a decoder choice with `matching_variant` or
`weights_file`. For MVM it can also set, for example:

```toml
mvm_epsilon = 0.01
mvm_seed = 1
```

`submit.sh mvm` sets `MVM_SEED` to `1` through `5` for the five jobs of each
graph, overriding `mvm_seed` in a config. This gives five independently
sampled matchings while the vertex weights remain fixed. Set the Slurm job
limit with `--time HH:MM:SS` when needed. The unfinished MVM runs on `math`
and `stackoverflow-answers` used a 60-hour limit (`--time 60:00:00`).
MVM also finishes early if every vertex is matched, since that result is
optimal for positive vertex weights.
MVM jobs alone request `--signal=B:USR1@600` from Slurm. `run.sh` replaces
its shell with the C++ program, which handles this signal by setting one
flag. The algorithm reads the flag between samples and after the first row
of a matched-center query. When the algorithm sees the flag at one of these
checks, it returns the last completed matching and writes a partial JSON
with the matching weight and sample count.
No clock is polled in the sample loop. A single row decode lasting beyond
the warning period can still be stopped by Slurm before a report is written.
An optional third argument supplies a different graph list when needed.
Each job writes one JSON file in `wg_algo` and a log in `logs/`. Its JSON
report includes the Slurm job ID, algorithm time, and process peak RSS.
The input load time is recorded separately because simultaneous reads from
shared storage can disturb it.

## Submit missing runs

After the earlier jobs finish, run this from `wg_algo` to reach five completed
reports for each graph and algorithm:

```bash
./submit_missing.sh --dry-run
./submit_missing.sh
```

The script reads `parameters.decoder` in the JSON reports already in
`wg_algo`, counts only bulk reports by default, and submits each graph and
algorithm's shortfall through `run.sh`. To submit one algorithm, use
`./submit_missing.sh matching` (or `bfs`, `dfs`, or `wvc`). For checking a copied
report directory without submitting, use
`./submit_missing.sh --dry-run all /path/to/reports`. Do not submit while earlier
jobs are still queued or running, because those jobs have no report yet.

To count and retry iterator reports, use:

```bash
./submit_missing.sh --dry-run --decoder iterator
./submit_missing.sh --decoder iterator
```

Iterator retries pass `config/iterator.toml` through `run.sh`. The script
requires at least one report in the selected mode, to avoid accidentally
submitting all 48 graphs when pointed at the wrong directory. Start a new
series with `submit.sh`, then use `submit_missing.sh` after those jobs have
finished. Collect bulk and iterator reports in separate evaluation folders;
the JSON field, rather than a filename suffix, identifies the mode.

For MVM, submit the first series with `./submit.sh mvm`, then inspect or
resubmit missing seeds with:

```bash
./submit_missing.sh --dry-run mvm
./submit_missing.sh mvm
```

This command checks the recorded `mvm_seed`, decoder, and config path against
the jobs it would submit. With no `--config` it counts only runs with default
epsilon and weights; an empty MVM report directory schedules all five seeds
for every graph. A `time_limit_partial` report counts as an observed run.
To rerun such seeds, add `--retry-partial`. For an iterator series add
`--decoder iterator`; if the original series used a custom MVM config,
pass it with `--config config.toml` so the retry uses the same epsilon and
weights. Keep that config's contents fixed between runs; the retry script
matches its path and does not compare previous contents. The `all` choice
remains the original four algorithms; invoke
`mvm` explicitly.
