# Negishi runs

Build the C++ program once in `zk_algo`:

```bash
make
```

`run.sh` reads a finished Zuckerli bundle from
`$RCAC_SCRATCH/fall2026/format_zk/GRAPH/GRAPH.zk`. Submit one graph from
the `zk_algo` directory:

```bash
sbatch run.sh matching math
```

`submit.sh` reads the graph names in `graphs.txt` and submits five
independent jobs for each graph:

```bash
./submit.sh matching
./submit.sh bfs
./submit.sh dfs
./submit.sh wvc
./submit.sh mvm
```

To report only a maximal matching, use a config containing
`matching_variant = "maximal"`. A WVC config may instead name a
`weights_file`. An optional graph-list argument narrows the submission.
`run.sh` owns the Slurm resource settings; `submit.sh` passes the algorithm,
graph, and optional config to it. Each job writes one JSON report in
`zk_algo` and a log in `logs/`.

For MVM, `submit.sh` passes `MVM_SEED=1, ..., 5` to the five jobs and requests
`--signal=B:USR1@600` from Slurm. The handler only sets a flag. When the
algorithm observes the flag between samples or after scanning the first row
for an already matched sampled vertex, it publishes the matching weight and
the count through the last completed sample. `submit.sh` requests the signal
for MVM submissions. For a direct submission that requests the warning, use
`sbatch --signal=B:USR1@600 run.sh mvm GRAPH`. Use `--time` to override the
job limit. The paper's unfinished MVM runs on `math` and
`stackoverflow-answers` used `--time 64:00:00`.

MVM accepts bare numeric `mvm_epsilon = 0.01` and `mvm_seed = 1` in a config;
`weights_file` uses the same canonical vertex order as WVC. The seed passed
by `submit.sh` takes precedence over the config's sampling seed. Generated
weights always use seed `19`.

After all submitted jobs have finished, `submit_missing.sh` reads the JSON
reports and submits only the runs needed to reach five per graph and algorithm.
Check its choices before submitting:

```bash
./submit_missing.sh --dry-run
./submit_missing.sh
```

If the existing reports were moved to an evaluation directory, pass that
directory for both commands. The script also counts newer reports still in
`zk_algo`; copies with the same Slurm job ID count once.

```bash
./submit_missing.sh --dry-run all ../evals_app/zk
./submit_missing.sh all ../evals_app/zk
```

The first argument may be one algorithm instead of `all`. For a retry that
needs more than `run.sh`'s four-hour limit, pass a Slurm time override such as
`--time 24:00:00` before the algorithm. The script changes the limit only for
these retry jobs. Run it again only after the previous retry jobs finish;
queued jobs have no JSON report yet.

`all` still means the four original algorithms; submit MVM explicitly with
`./submit_missing.sh mvm REPORT_DIR`. MVM retry checks each graph and each of
the five sampling seeds for default epsilon and generated weights, including
reports copied to `REPORT_DIR`; reports with custom settings are ignored. A partial
report counts as an observed run. To rerun partial seeds from an empty
matching, use `./submit_missing.sh --retry-partial --time 24:00:00 mvm REPORT_DIR`.
Use `--dry-run` first to see the selected seeds.

The JSON records Slurm job ID, algorithm time, load time, and process peak
RSS. Because concurrent reads can disturb shared-filesystem timing, compare
algorithm time after load for the main experiment. Keep the independent JSON
files until all intended repetitions have finished.
