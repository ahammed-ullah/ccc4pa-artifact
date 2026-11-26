# Paper artifact

This is the artifact workspace for
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).

| Path | Contents |
| --- | --- |
| `repos/` | Frozen source snapshots of the seven research repositories used for the paper |
| `repo-versions.tsv` | Original snapshot and experiment source revisions, with release commits for the standalone code repositories |
| `data/datasets/` | Dataset descriptions, source links, generation settings, and graph metadata |
| `data/evals_con/run-reports/` | Saved clique-cover and Feder–Motwani reports, WebGraph encoding and search records, and Zuckerli encoding records |
| `data/evals_con/summaries/` | Construction workbooks and summary tables |
| `data/evals_app/run-reports/` | Saved graph algorithm results, timing, and memory reports |
| `data/evals_app/summaries/` | Application workbooks and summary tables |
| `metadata/run-scripts/` | Experiment instructions and any launchers or configurations kept with the artifact |
| `metadata/analysis/` | Scripts that turn saved reports into summaries |
| `metadata/figures/` | Source that renders the experimental figures |
| `docs/` | Paper landing page source |

The `snapshot_commit` column identifies each source export before release
preparation. The `experiment_source_commit` column identifies the revision used
for the experiment. Where present, `release_commit` identifies the exact files
bundled here and prepared for the standalone public repository. A dash marks a
component distributed only in this artifact. Release
preparation replaces machine-specific data paths with portable references.
[Path references in the saved records](data/datasets/README.md)

## `cc_con` examples

The frozen `cc_con` snapshot carries the same examples as the public repo.
From `repos/cc_con/`, build with `make` and follow the examples to generate a
small random intersection graph or run on the Matrix Market version of
coPapersCiteseer used in the paper. [Example guide](repos/cc_con/examples/README.md)

## Construction experiment data

The saved data contains 1,440 construction reports, 102 lower-bound reports,
and 48 BPA+ CCF-writing reports. The Feder–Motwani baseline has five saved runs
at the chosen setting for each of the same 48 graphs. The manifest records the
experiment and snapshot commits for both repos. See the
[clique-cover run instructions](metadata/run-scripts/cc_con/README.md),
[Feder–Motwani run instructions](metadata/run-scripts/fm_con/README.md), and
[dataset metadata](data/datasets/README.md).

From the artifact root, use Python 3 with `openpyxl` to rebuild the chart-free
construction workbook from the saved reports and the workbook template. The
builder was verified with `openpyxl` 3.0.10.

```bash
python3 metadata/analysis/build-clique-cover-construction-summary.py
```

The command writes
[`clique-cover-construction-and-baselines.xlsx`](data/evals_con/summaries/clique-cover-construction-and-baselines.xlsx).

The WebGraph and Zuckerli records supply the construction measurements for
Figure 23. The run guides describe how the selected WebGraph order is used
by both encoders and identify the size and timing fields used in the paper.
[WebGraph construction run guide](metadata/run-scripts/wg_con/README.md) · [Zuckerli construction run guide](metadata/run-scripts/zk_con/README.md)

## Application experiment data

The saved reports contain five repetitions for each of 48 graphs and five
algorithms in each representation. Plain and VByte each contribute 1,200
reports. The shared WebGraph and Zuckerli reports supply the comparisons.
The [clique-cover run guide](metadata/run-scripts/cp_algo/README.md) describes
how to prepare the input cover and repeat a run. The baseline guides record
the selected bundles and the time limits for the unfinished runs.
[WebGraph run guide](metadata/run-scripts/wg_algo/README.md) · [Zuckerli run guide](metadata/run-scripts/zk_algo/README.md)

From the artifact root, use Python 3 to rebuild the two chart-free workbooks.

```bash
python3 metadata/analysis/build-application-summaries.py
```

This rebuilds the [runtime and peak-memory workbook](data/evals_app/summaries/application-runtime-and-peak-memory.xlsx)
and the [solution-values workbook](data/evals_app/summaries/application-solution-values.xlsx).
For each graph and algorithm, take the median of the five measurements in
each representation. Figure 12 divides the WebGraph or Zuckerli median by the
Plain or VByte median. Figure 13 compares the VByte medians directly with the
two baselines. The saved vertex-weighted matching reports mark the
time-limited runs on `math` and `stackoverflow-answers` in both baselines.
Those measurements are lower bounds on the time and peak memory of a
completed run. The solution workbook preserves the reported weights as
exact decimal values for Appendix C.3.

## File names

Use report names that identify the experiment and retain the recorded run identifiers. Give summaries and analysis scripts names that describe what they produce.

| Current workbook | Proposed artifact workbook |
| --- | --- |
| `time_con.xlsx` | `graph-representation-construction-times.xlsx` |
| `encoding_size.xlsx` | `graph-representation-sizes.xlsx` |
| `time_rss_raw.xlsx` | `application-runtime-and-peak-memory.xlsx` |
| `solution_values.xlsx` | `application-solution-values.xlsx` |

When an analysis script moves or its output is renamed, compare the regenerated summary values with the saved workbook.

## Paper results from `cc_con` and shared inputs

| Figure or table | Saved inputs | Values used |
| --- | --- | --- |
| Tables 2, 5, and 6 | [Dataset metadata](data/datasets/README.md) and the archived [graph specifications](repos/cc_con/config/graphs/) | Graph collection, canonical vertex and edge counts, and synthetic generation settings |
| Figure 11 | [Construction reports](data/evals_con/run-reports/cc_con/construction/) and [Feder–Motwani reports](data/evals_con/run-reports/fm_con/construction/) | `comp_cr` and `comp_time` in the construction workbook. Compression divides `2m` by the total clique-cover or biclique-partition size. Construction time is the median of five runs. |
| Figures 12 and 13 | [Application reports](data/evals_app/run-reports/) | Medians from the application runtime and peak-memory workbook, using the marked partial results where a baseline reached its time limit |
| Table 4 | Construction reports and [lower-bound reports](data/evals_con/run-reports/cc_con/lower-bounds/) | Divide `cover size` values for LZF, BCC, FPA-AM, and CPA-AM by the maximum available bound per graph in `lb_sigma`. Count the graphs at or below each threshold in the table. |
| Figure 21 | Construction and Feder–Motwani reports | `comp_cr` in the construction workbook. Group by collection and order the plotted graphs by average degree. |
| Figure 22 | Construction reports | `comp_cr_am` and `comp_time_am` in the construction workbook. Order the graphs by average degree for compression and by edge count for time. |
| Figure 23 | [BPA+ CCF-writing reports](data/evals_con/run-reports/cc_con/bpa-vbyte-cover-construction/), [WebGraph measurements](data/evals_con/run-reports/wg_con/), and [Zuckerli measurements](data/evals_con/run-reports/zk_con/) | [Encoding sizes](data/evals_con/summaries/graph-representation-sizes.xlsx) and [construction times](data/evals_con/summaries/graph-representation-construction-times.xlsx) |

The Figure 23 cover encoding size is `output.encoded_bytes` from the BPA+ report.
Its construction time sums `constructed_cover.time_seconds`,
`postprocess.time_seconds`, and `output.encoding_and_write_time_seconds`.
Match records by graph name to the selected WebGraph bundle and Zuckerli bundle.
Figure 23 uses WebGraph's `selected_attempt_seconds`, which includes
single-threaded encoding and indexing with the vertex order already available.
The separate `successful_attempt_seconds` subtotal includes any multithreaded
BFS+LLP ordering done inside successful trials and gives a lower bound on the
full per-graph search time by excluding interrupted trials and work outside
the timed subprocesses.
