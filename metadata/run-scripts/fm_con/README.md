# Feder–Motwani construction runs

These runs supply the Feder–Motwani baseline for Section 5.2 and Appendix C.2 of
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).

From the artifact root, go to `repos/fm_con/` and build with `make`.
Set `CGF` to the absolute path of the canonical `.cgf` produced by
[`cc_con` graph preparation](../cc_con/README.md) and `DELTA` to the graph's
selected value in the run lists below. Run the command five times per graph to
repeat the timing measurements.
[Frozen `fm_con` source](../../../repos/fm_con/README.md)

```bash
./bin/fm_con --graph "$CGF" --delta "$DELTA"
```

| Negishi runs | Selected deltas | Submit script |
| --- | --- | --- |
| 36 graphs, four-hour limit | [`selected_delta.txt`](../../../repos/fm_con/selected_delta.txt) | [`submit.sh`](../../../repos/fm_con/submit.sh) |
| 11 graphs, 24-hour limit | [`selected_delta_long.txt`](../../../repos/fm_con/selected_delta_long.txt) | [`submit_long.sh`](../../../repos/fm_con/submit_long.sh) |
| `math`, high-memory queue | [`selected_delta_large.txt`](../../../repos/fm_con/selected_delta_large.txt) | [`submit_highmem.sh`](../../../repos/fm_con/submit_highmem.sh) |

Automatic search selected the delta for 38 graphs. For the other 10, the
selected delta gave the smallest partition size among completed fixed-delta
trials. Every timing run uses the resulting fixed delta and linear workspace.

The [240 saved JSON reports](../../../data/evals_con/run-reports/fm_con/construction/)
feed the [chart-free construction workbook](../../../data/evals_con/summaries/clique-cover-construction-and-baselines.xlsx).
The workbook uses `2 * graph.m / partition.total_size` for compression and the
median of five `timing.selected_run_seconds` values for construction time.
This time covers the selected run. Input-index construction is recorded
separately, and the timing runs use a preselected delta.
