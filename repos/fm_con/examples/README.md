# Example

The included `tiny.cgf` is a canonical graph with 5 vertices and 4 undirected
edges. From the repository root:

```sh
make
mkdir -p /tmp/fm-example
cd /tmp/fm-example
/path/to/fm_con/bin/fm_con --graph /path/to/fm_con/examples/tiny.cgf
```

This graph has no attainable positive initial `k_hat`, so the result directly
partitions all 8 directed cross edges into one-edge parts. The generated
`fm-tiny.json` therefore reports:

```text
initial_cross_edges = 8
nontrivial_size     = 0
residual_edges      = 8
total_size          = 16
m_star              = 13
n_star              = 10
```

For an experimental graph, replace `tiny.cgf` with the absolute path to the
prepared CGF file. No graph or run configuration file is used by `fm_con`.

Adding `--delta 0.5` runs only that value and names this example's report
`fm-tiny-delta-0.5.json`. Omitting the option uses automatic delta selection.
