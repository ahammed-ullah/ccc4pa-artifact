# Canonical Binary Formats

The project defines two related but independent binary formats:

- Canonical Graph Format (`.cgf`) stores one sorted open neighborhood per
  canonical vertex.
- Clique Cover Format (`.ccf`) stores one sorted clique per cover label.

Both formats are little endian, versioned, checksummed, and allow one record to
be located and decoded without reading earlier records. Counts and offsets are
unsigned 64-bit values. The implementation still
rejects graph orders above `INT_MAX` because construction vertex IDs are `int`,
and the implementation assumes the number of output cliques also fits in `int` because
clique labels use the same type.

## Shared record encoding

For `R` records, a file stores `R+1` absolute `uint64` offsets. Record `i`
occupies `[offset[i], offset[i+1])`; `offset[0]` is the payload start and
`offset[R]` is the file/payload end. This needs `8(R+1)` directory bytes rather
than one `(offset,length)` pair per record.

Here the *payload* is the part of the file containing the encoded neighborhoods
or cliques, after the fixed header and offset table.

Each record contains:

```text
cardinality:vbyte, first_id:vbyte, gap_1:vbyte, ..., gap_(cardinality-1):vbyte
```

IDs must be strictly increasing. The first ID is stored directly and every
later value is its positive difference from the previous ID. Vbyte uses seven
low data bits per byte; a high bit of one means another byte follows. Integers
must use their shortest encoding. Cardinality makes record validation explicit,
while the two adjacent offsets let a decoder seek to one record without reading
its predecessors.

## Fixed 160-byte headers

CGF and CCF use the same physical header positions where the fields have the
same meaning:

| Offset | Bytes | CGF | CCF |
| ---: | ---: | --- | --- |
| 0 | 8 | `CGF\0\r\n\x1a\n` | `CCF\0\r\n\x1a\n` |
| 8 | 4 | format version `1` | format version `1` |
| 12 | 4 | header bytes `160` | header bytes `160` |
| 16 | 4 | encoding ID `1` | encoding ID `1` |
| 20 | 4 | flags `0` | flags `0` |
| 24 | 8 | canonical `n` | canonical `n` |
| 32 | 8 | canonical `m` | canonical `m` |
| 40 | 8 | neighborhood records, equal to `n` | number of cliques |
| 48 | 8 | adjacency entries, equal to `2m` | cover size `sum |C[i]|` |
| 56 | 8 | offset-table position, `160` | offset-table position, `160` |
| 64 | 8 | offset count, `n+1` | offset count, `num_cliques+1` |
| 72 | 8 | payload position | payload position |
| 80 | 8 | payload bytes | payload bytes |
| 88 | 8 | complete file bytes | complete file bytes |
| 96 | 32 | canonical graph SHA-256 | canonical graph SHA-256 |
| 128 | 32 | file SHA-256 | file SHA-256 |

Encoding ID `1` is cardinality-prefixed sorted-ID delta-vbyte. Future encodings
use a new ID without changing the `.cgf` or `.ccf` suffix.

The file checksum is SHA-256 over the complete file after treating bytes
`128..159` as zero. It detects corruption in the header, directory, and payload.
The graph fingerprint is logically separate: it identifies the canonical graph
independently of physical encoding.

## Canonical graph fingerprint

The fingerprint is SHA-256 over this logical byte stream:

```text
"CC-CON-CANONICAL-GRAPH-v1\0"
n:u64
m:u64
(u:u64, v:u64) for every edge u < v in lexicographic order
```

All integers are little endian. Including IDs makes the fingerprint sensitive
to the exact canonical labeling. `cc_check graph` recomputes it from decoded
adjacency; the normal `cc_con` decoding path reads the value stored by
`cg_prepare` without recomputing it.

CCF carries the same value so `cc_check` can reject a cover paired with a
different graph before semantic verification.

## CGF files

The preparation JSON uses the field name `artifact` for the single prepared
CGF file and its encoding metadata. This format field is distinct from the
paper's *evaluation artifact*, which means the complete released code, data
descriptions, and results.

Run:

```text
cg_prepare --graph source.toml --output-dir prepared
```

For graph name `email-enron`, the command writes:

```text
prepared/email-enron.cgf
prepared/email-enron.cgf.json
prepared/email-enron.cgf.toml
```

The JSON companion file preserves original source parameters and
generation/projection statistics, canonical `n` and `m`, both hashes, CGF
bytes, timings, and its schema/format versions. `schema_version` versions the
JSON field layout; `format_version` separately versions the binary CGF layout.
The generated TOML points to the other two files and records the JSON file's
SHA-256 checksum. It is
written last as the preparation completion marker.

## CCF output

CCF always stores the final cover after optional assignment-minimal
postprocessing. Configure it with:

```toml
[output]
write_cover = true
directory = "/absolute/path/covers"
```

Graph `email-enron` produces `email-enron.ccf`. The writer encodes one record
at a time without constructing a second encoded copy in memory. The report's
`encoded_bytes` is the complete CCF size, including header, offsets, and payload;
`cover_size` remains a count of vertex-to-clique assignments, not bytes.

Requesting CCF output implies semantic verification. Structurally valid CCF can
be checked without a graph, while exact clique and edge-coverage verification
uses any supported graph specification:

```text
cc_check cover --cover email-enron.ccf
cc_check cover --cover email-enron.ccf --graph email-enron.cgf.toml
```
