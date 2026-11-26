# WebGraph application runs

These runs supply the WebGraph results in Figures 12 and 13 and the
solution-quality comparisons in Appendix C.3.
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096)

## Inputs and settings

Use the selected WebGraph bundle from `wg_con` for each of the 48 graphs.
It contains `.graph`, `.properties`, and `.ef`. Include `.perm` for reordered
graphs so the weighted algorithms can use the original vertex weights. The
saved report for each run records the input file sizes.
[WebGraph construction guide](../wg_con/README.md) · [Selected WebGraph bundles](../../../data/evals_con/run-reports/wg_con/selected-encoding-sizes.csv) · [Graph list](../../../repos/wg_algo/graphs.txt)

Build with `make` in `repos/wg_algo/`. The five runs of each graph and
algorithm used the default `bulk` decoder, with no config file. The default
matching command selects the 3/2 approximation. The weighted algorithms use
the same vertex weights as `cp_algo`, generated with seed 19 in original
vertex order. For vertex-weighted matching, the error parameter was 0.01.
Use a different sampling seed for each repetition.
[Commands and defaults](../../../repos/wg_algo/README.md) · [Weight generation](../../../repos/wg_algo/docs/weights.md)

On Negishi, `submit.sh` passes seeds 1 through 5 for vertex-weighted matching.
For the unfinished runs, use the 60-hour limit. Run the following command
from the frozen `wg_algo` directory with a graph list containing `math` and
`stackoverflow-answers`. The empty argument selects the default settings.

```sh
./submit.sh --time 60:00:00 mvm "" /absolute/path/to/graph-list.txt
```

The launcher requests a warning ten minutes before the job limit. All five
saved runs on each of these graphs stopped after about 59 hours and 50 minutes
of algorithm time and saved the matching reached at that point.
[Slurm instructions](../../../repos/wg_algo/docs/slurm.md)

## Saved results

Use the median of the five saved times and the median of the five saved peak
memory measurements for each graph and algorithm. For the two unfinished
vertex-weighted matching cases, the saved measurements are lower bounds on a
completed run. The timing workbook identifies these partial runs by seed.
[1,200 WebGraph reports](../../../data/evals_app/run-reports/wg_algo/) · [Timing and memory workbook](../../../data/evals_app/summaries/application-runtime-and-peak-memory.xlsx) · [Solution values](../../../data/evals_app/summaries/application-solution-values.xlsx)
