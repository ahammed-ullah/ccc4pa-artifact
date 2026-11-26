# Examples

Run `make` from the repository root before using these commands. This creates
the four programs under `bin/`.

The graph and run specifications are independent. Choose one file from
`graphs/`, choose the run file for the desired construction from `runs/`, and
pass both to the construction command:

```bash
bin/cc_con <algorithm> \
  --graph examples/graphs/<graph>.toml \
  --run examples/runs/<algorithm>.toml
```

## Generate a small random intersection graph

This runs BPA on a graph generated locally from a k-uniform hypergraph with
100 initial vertices and a fixed seed.

```bash
bin/cc_con bpa \
  --graph examples/graphs/hypergraph-k-uniform.toml \
  --run examples/runs/bpa.toml
```

A successful run writes `bpa-hypergraph-k-example.json` in the current directory. Repeating
the command selects the next unused numeric suffix rather than replacing that
report. Report names are reserved before construction, so concurrent repetitions
also receive distinct files.

To run every construction on the same graph:

```bash
for algorithm in bcc bpa cpa dpf fpa lzf; do
  bin/cc_con "$algorithm" \
    --graph examples/graphs/hypergraph-k-uniform.toml \
    --run "examples/runs/${algorithm}.toml"
done
```

## Run on a SuiteSparse graph

Download the Matrix Market archive for coPapersCiteseer, one of the paper's
benchmark graphs. Extract `coPapersCiteseer.mtx` to
`datasets/suitesparse/coPapersCiteseer/` at the repository root.
[SuiteSparse Matrix Collection](https://sparse.tamu.edu/DIMACS10/coPapersCiteseer)

Run the supplied graph specification from the repository root.

```bash
bin/cc_con bpa \
  --graph config/graphs/coPapersCiteseer.toml \
  --run examples/runs/bpa.toml
```

Use the same specification format with other SuiteSparse Matrix Market files.
Set the name and path to match the graph you downloaded.

## Graph Specifications

Every file below is runnable from a fresh checkout. The small files under
`data/` are deliberately constructed examples of the accepted syntax; they are
not excerpts from the evaluation datasets.

| Graph specification | Source exercised |
| --- | --- |
| `graphs/matrix-market.toml` | Matrix Market coordinate input |
| `graphs/hypergraph-k-uniform.toml` | K-uniform hypergraph projection |
| `graphs/hypergraph-poisson.toml` | Poisson hyperedge-size projection |
| `graphs/hypergraph-lognormal.toml` | Lognormal hyperedge-size projection |
| `graphs/hypergraph-power-law.toml` | Discrete power-law hyperedge-size projection |
| `graphs/cornell-simplices.toml` | Cornell parallel `nverts` and `simplices` files |
| `graphs/cornell-dual-simplices.toml` | Incidence-dual projection of the same files |
| `graphs/cornell-hyperedges.toml` | Cornell comma-separated hyperedge lists |
| `graphs/stack-exchange-tag-question.toml` | Stack Exchange tag-question assignments |

For a downloaded dataset, copy the corresponding graph specification and
replace its example path or generation parameters. Relative paths are resolved
against the graph specification, not against the shell's current directory.

## Run Specifications

The six algorithm-named files request statistics only: assignment-minimal
postprocessing and explicit verification are disabled. BPA, FPA, and CPA state
the default `tau = 4.0` and `min-label` selection rule explicitly. BCC, DPF, and
LZF have no construction parameters.

`runs/write-cover.toml` demonstrates CCF output using the default construction
parameters. It can be used with any construction. Its output directory must
exist first:

```bash
mkdir -p build/example-output
bin/cc_con bpa \
  --graph examples/graphs/hypergraph-k-uniform.toml \
  --run examples/runs/write-cover.toml
```

This writes `build/example-output/hypergraph-k-example.ccf`. Requesting a CCF
automatically performs semantic cover verification before writing the file.

## Prepared CGF Input

A CGF graph specification is generated together with the encoded graph and its
metadata report, so it is not maintained as a handwritten example. Prepare the
small random intersection graph with:

```bash
mkdir -p build/example-prepared
bin/cg_prepare \
  --graph examples/graphs/hypergraph-k-uniform.toml \
  --output-dir build/example-prepared
```

The generated specification is then used exactly like the other graph files:

```bash
bin/cc_check graph \
  --graph build/example-prepared/hypergraph-k-example.cgf.toml
bin/cc_con lzf \
  --graph build/example-prepared/hypergraph-k-example.cgf.toml \
  --run examples/runs/lzf.toml
```

`cc_bound` takes the encoded CGF file itself rather than its generated TOML:

```bash
bin/cc_bound local-mis --truss \
  --graph build/example-prepared/hypergraph-k-example.cgf
```

The complete five-command example is in [`bounds/README.md`](bounds/README.md).
