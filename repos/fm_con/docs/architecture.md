# Project Structure

```text
fm_con/
|-- Makefile
|-- README.md
|-- selected_delta.txt
|-- selected_delta_long.txt
|-- selected_delta_large.txt
|-- graph_delta.txt
|-- graph_delta_large.txt
|-- run.sh, run_highmem.sh
|-- submit.sh, submit_long.sh, submit_highmem.sh
|-- submit_delta.sh, submit_delta_highmem.sh
|-- include/fm_con/
|   |-- big_uint.hpp
|   |-- cgf.hpp
|   |-- fm.hpp
|   |-- linear_index.hpp
|   |-- neighborhood_tree.hpp
|   `-- report.hpp
|-- src/
|   |-- algo_fm.cpp
|   |-- big_uint.cpp
|   |-- cgf.cpp
|   |-- linear_index.cpp
|   |-- main.cpp
|   |-- neighborhood_tree.cpp
|   `-- report.cpp
|-- docs/
|-- examples/
`-- tests/
```

`src/main.cpp` parses `--graph`, optional `--delta`, and optional `--workspace`,
reserves a report name, runs the construction, and publishes the report.

`src/cgf.cpp` is the project's independent CGF version 1 decoder. There is no
generic graph-reader layer because no other input format is accepted.

`src/algo_fm.cpp` contains fixed-delta execution, automatic delta selection,
Clique Stripping, the two separate `k_hat >= 2` workspace loops, the exact
`k_hat=1` specialization, candidate-run comparison, and the size accounting.

`src/linear_index.cpp` contains the default packed adjacency, deleted-edge
bits, and blocked Fenwick counts. The immutable adjacency is shared across
candidate runs; mutable state is created separately for each run.

`src/neighborhood_tree.cpp` builds and updates the sparse neighborhood trees
used by the optional prefix-tree workspace while `k_hat>=2`.

`src/big_uint.cpp` supplies only the exact integer operations needed by the
conditional scores. It has no external library dependency.

`src/report.cpp` atomically reserves report names and writes the selected run as
JSON. Name reservation and JSON writing are outside the construction timings.
The construction returns counters, not stored biclique parts.

The selected-delta files partition the evaluation graphs into four-hour,
24-hour, and high-memory batches and provide one fixed value per graph. Each
drives a five-run timing batch. The graph-delta files retain the separately
submitted selection trials. Both kinds of scripts call the same executable and
do not change its automatic-selection or fixed-delta procedures.

`tests/test_main.cpp` includes a slow direct implementation for small graphs.
The tests compare its partition statistics with both workspace methods,
including a graph whose vertex count is not a power of two. A larger test
crosses the 256-edge rank boundary during actual deletions.
`tests/integration.sh` exercises the default release command and parses its
JSON.
