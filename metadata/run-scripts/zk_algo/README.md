# Zuckerli application runs

These runs supply the Zuckerli results in Figures 12 and 13 and the
solution-quality comparisons in Appendix C.3.
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096)

## Inputs and settings

Use the modified Zuckerli bundle from `zk_con` for each of the 48 graphs.
Each bundle contains `.zk` and `.zki`. Reordered graphs also have `.perm`
so the weighted algorithms can use the original vertex weights. Zuckerli
reused the vertex order selected for WebGraph. Each application report
records the input file sizes.
[Zuckerli construction guide](../zk_con/README.md) · [Selected Zuckerli bundles](../../../data/evals_con/run-reports/zk_con/encoding-times-and-sizes.csv) · [Graph list](../../../repos/zk_algo/graphs.txt)

Build with `make` in `repos/zk_algo/`. The five runs per graph and algorithm
used the defaults with no config file. Matching uses the 3/2 approximation.
Weighted vertex cover and vertex-weighted matching use weights generated
with seed 19 in original vertex order. Vertex-weighted matching uses
`ε = 0.01` and a separate sampling seed for each repetition. On Negishi,
`submit.sh` passes seeds 1 through 5 for vertex-weighted matching.
[Commands and defaults](../../../repos/zk_algo/README.md) · [Algorithm notes](../../../repos/zk_algo/docs/algorithms.md)

For the unfinished runs, use the 64-hour limit. From `repos/zk_algo/`, run
the following command with a graph list containing `math` and
`stackoverflow-answers`. The empty argument selects the default settings.

```sh
./submit.sh --time 64:00:00 mvm "" /absolute/path/to/graph-list.txt
```

The launcher requests a Slurm warning ten minutes before the job limit. All
five runs on each of these graphs saved the matching reached after about
63 hours and 50 minutes of algorithm time.
[Slurm instructions](../../../repos/zk_algo/docs/slurm.md)

## Saved results

For each graph and algorithm, use the median of the five algorithm times
and the median of the five process peak RSS measurements. The timing workbook
marks partial runs by seed. Their observations are lower bounds for a run
that continues to completion.
[1,200 Zuckerli reports](../../../data/evals_app/run-reports/zk_algo/) · [Timing and memory workbook](../../../data/evals_app/summaries/application-runtime-and-peak-memory.xlsx) · [Solution values](../../../data/evals_app/summaries/application-solution-values.xlsx)

The experiment source was confirmed after the runs and is recorded in the
manifest. The snapshot commit adds documentation and removes Finder metadata.
[Repository revisions](../../../repo-versions.tsv)
