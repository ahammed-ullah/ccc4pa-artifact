# Lower-bound example

Build the programs and prepare the small random intersection graph once.

```bash
make
bin/cg_prepare \
  --graph examples/graphs/hypergraph-k-uniform.toml \
  --output-dir build/bound-example
```

Then run any of the five supported bound configurations directly on the CGF:

```bash
cd build/bound-example
../../bin/cc_bound linear --graph hypergraph-k-example.cgf
../../bin/cc_bound local-mis --graph hypergraph-k-example.cgf
../../bin/cc_bound local-mis --truss --graph hypergraph-k-example.cgf
../../bin/cc_bound local-color --graph hypergraph-k-example.cgf
../../bin/cc_bound local-color --truss --graph hypergraph-k-example.cgf
```

Each command writes a separate JSON report in `build/bound-example` and leaves
the prepared CGF unchanged.
