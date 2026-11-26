# Vertex weights

The default WVC and MVM instances are identical to `cp_algo`'s. For original vertices
`0, ..., n-1`, a C++ `mt19937_64` engine seeded with `19u` draws one positive
weight in `[1,n²]` per vertex. The mapping avoids bias and is independent of
the C++ library's `uniform_int_distribution` implementation:

```text
bound = n*n
threshold = (0 - bound) modulo 2^64, then modulo bound
repeat draw = engine() while draw < threshold
weight = 1 + (draw modulo bound)
```

A config can instead contain `weights_file = "weights.txt"`. The file must
provide exactly `n` positive decimal unsigned 64-bit integers, one per
nonempty line, in original vertex order. Blank lines and `#` comments are
allowed. A relative path is resolved from the config file's directory.

When `.perm` exists, entry `perm[new]` is the original ID of the vertex at
WebGraph position `new`. The program reorders weights before the algorithm
timer. Residual and original weights use unsigned 64-bit integers. WVC and
MVM reported sums use unsigned 128-bit arithmetic and appear in JSON as
exact decimal strings. MVM's sampled centers use a separate `mt19937_64`
engine; its default seed is `1`. `mvm_seed` sets it in a config, and
`MVM_SEED` overrides that value for independent Slurm runs. Sampling uses
the same unbiased rejection rule with bound `n`. A fixed seed samples the
same sequence of WebGraph IDs. If the vertex order changes, those IDs can
refer to different original vertices. Each draw remains uniform.
