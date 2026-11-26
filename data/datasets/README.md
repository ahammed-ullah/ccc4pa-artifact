# Graph metadata

[`graphs.txt`](graphs.txt) lists the 48 graph instances in the paper's collection
order. [`summary.md`](summary.md) gives canonical vertex counts, edge counts,
and densities. Each `*.cgf.json` preparation report records the canonical
graph fingerprint and the source or generation settings.

Use the `[graph].name` field to match a report to the
[frozen graph specifications](../../repos/cc_con/config/graphs/). The 18 random
intersection graph specifications record the generator settings and seeds.
For a real graph, download the source and follow the setup in the
[frozen `cc_con` instructions](../../repos/cc_con/README.md) before running
`cg_prepare`. The `cc_con` graph specifications look for raw inputs under
`datasets/` in the `cc_con` repository.

Saved report paths beginning with `format_std/`, `format_cgf/`, `format_ccf/`,
`format_wg/`, and `format_zk/` are relative to your external data root. They
identify the raw inputs and generated representations used for the experiments.
Paths beginning with `repos/` are relative to the artifact root. The application
reports use `cp_algo/` and `cp_algo4/` as labels for the two original configuration
locations.

For an example with a public Matrix Market download, use coPapersCiteseer.
[SuiteSparse Matrix Collection](https://sparse.tamu.edu/DIMACS10/coPapersCiteseer)
