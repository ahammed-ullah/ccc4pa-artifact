# Source audit and changes

This encoder follows the public [Google Zuckerli source](https://github.com/google/zuckerli)
where it implements the coding steps in the paper. The locations below refer
to the inspected `src/` files. `zk_con` only **constructs** compressed files.
Decoder issues found during the audit are recorded separately because their
fixes belong in [`zk_algo`](../../zk_algo/README.md). The `.zk`/`.zki` files
are a modified format and are not byte-compatible with upstream Zuckerli.

## Encoder defects fixed here

- **Flags do not select the advertised settings.** `src/common.h:70` sets
  `ZKR_HONOR_FLAGS` to 0. As a result, `src/context_model.h:14–21` uses
  `W=32`, and `src/integer_coder.h:44–63` uses hybrid `(4,2,1)`, regardless of
  the command-line flags in `src/flags.cc:6–15`. Enabling flags also exposes a
  reference-window limit of 64 and a missing `FLAGS_num_token_lsb` declaration.
  [`format.hpp`](../src/format.hpp) fixes the settings used by this experiment;
  [`codec.cpp`](../src/codec.cpp) implements `(3,1,0)`. With `C=1`, the
  previous-reference context resets to zero for every row, so a window of 128
  does not require 128 reference contexts.
- **The no-reference candidate can use an old copied-edge list.** In
  `src/encode.cc:290–309`, the encoder evaluates that candidate before clearing
  `adj_block`, which may still contain edges from the previous row's last
  candidate. This can change its estimated cost and selected reference.
  [`emit_parts`](../src/encode.cpp) clears the copied list before every
  candidate, including the no-reference case.
- **The model histogram counts zeros removed by run-length coding.** In
  `src/encode.cc:275–284,391–417`, `token_cost` increments a symbol count, but
  `rle_undo` only subtracts estimated cost. The zero symbols replaced by a run
  token remain in the histogram used for the next reference-selection round.
  [`make_row_tokens`](../src/encode.cpp) first constructs the tokens that will
  actually be written, and the model counts those tokens.
- **Large integer shifts use signed `1`.** `src/integer_coder.h:68–85,98–107`
  contains shifts such as `1 << n`, which are undefined for sufficiently large
  values. Our [`codec.cpp`](../src/codec.cpp) uses `uint64_t{1}` and checks bit
  widths.
- **The debug-assert switch is reversed.** `src/common.h:26–35` removes
  `ZKR_DASSERT` in debug builds and enables it otherwise. This encoder does
  not use that macro; its input and coding checks use ordinary exceptions.

## Construction choices for this experiment

- **CCF input.** Upstream `src/uncompressed_graph.h:66–96` and
  `src/uncompressed_graph.cc:47–55` expect a mapped adjacency-array file.
  [`CcfGraph`](../src/ccf_graph.cpp) instead reads the final CCF cover, builds
  vertex-to-clique incidence, and forms one sorted, distinct neighborhood at a
  time. Reference selection retains at most 129 recent neighborhoods.
- **Streaming output.** Upstream `src/encode.cc:254,423–470` keeps all tokens
  in `IntegerData` (`src/integer_coder.h:126–176`) until Huffman encoding.
  [`encode_graph`](../src/encode.cpp) makes separate passes for the reference
  choices and histograms, then writes one row's tokens at a time. It retains
  an `n`-entry reference array and, while writing, an `n+1`-entry bit-offset
  array, but no graph-wide token stream.
- **Unlimited reference chains.** Upstream `src/encode.cc:286–324` applies a
  chain-length limit. We omit that step for the fixed `R=∞` choice, as allowed
  by the paper's unrestricted-reference case. It can make a future
  neighborhood request expensive.
- **Explicit format settings.** Upstream `src/encode.cc:249–253` writes a node
  count and access-mode bit, while `src/compressed_graph.cc:27–31` assumes
  matching compiled-in coding settings. Our [64-byte header](../src/format.cpp)
  records the chosen settings, counts, and CCF graph fingerprint. The encoder
  also writes the packed `.zki` row-offset index. Both files count toward the
  compressed bundle size.

## Decoder findings addressed in `zk_algo`

These are findings in the upstream decoder. No decoder runs in `zk_con`, and
the construction manifest does **not** claim exact-row verification. The
[`zk_algo` audit](../../zk_algo/docs/upstream-audit.md) describes its reader
and traversal choices.

- `src/compressed_graph.cc:43–54,82–85` passes the whole file length to a
  `BitReader` whose pointer has already advanced to a row; near the file end,
  that can make the reader step beyond its allocation. `src/bit_reader.cc:17–18`
  also forms `data + size - 8` for inputs shorter than eight bytes. `zk_algo`
  uses a bit reader bounded by the actual row start and end.
- `src/compressed_graph.cc:133–135` follows reference chains recursively.
  Unlimited chains require iterative traversal to avoid stack overflow;
  `zk_algo` resolves them iteratively.
- `src/compressed_graph.cc:34–40` scans the full graph on open to discover row
  starts. `zk_algo` reads the `.zki` offsets produced during construction.
- `src/huffman.h:51–52` reports a successful final state without checking the
  final bit position. `zk_algo` requires each row to stop exactly at its
  recorded end bit and rejects malformed tables or truncated files.
- `src/roundtrip_test.cc:22–40` exercises sequential decoding and a checksum,
  but not the on-demand `Neighbours(u)` method. `zk_algo` tests exact row
  comparisons, including empty rows and long reference chains, and provides
  `zk_check` for whole-graph validation.

The paper's main list-access experiments use `R=3`, `C=32`, `W=32`, and
`(k,i,j)=(4,1,0)`; see its [parameter choices](https://arxiv.org/html/2009.01353#S4.SS2).
Our fixed choices are listed in [paper-mapping.md](paper-mapping.md). Results
must be labeled as this variant, not as a rerun of the paper's binary.
