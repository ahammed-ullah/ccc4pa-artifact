# Canonical format fixtures

The `.hex` files are fixed binary fixtures represented as whitespace-separated
hexadecimal bytes so they remain reviewable in source control. Tests materialize
them under `build/format-fixtures/` before calling the production readers.

The valid graph has vertices `0..4` and edges
`01, 02, 12, 23, 24, 34`: two triangles sharing vertex `2`. Its valid cover is
`{0,1,2}, {2,3,4}`.

- `two-triangles.cgf.hex`: valid CGF v1 graph.
- `two-triangles.ccf.hex`: valid CCF v1 cover.
- `bad-checksum.ccf.hex`: payload corruption with the original checksum.
- `bad-offset.ccf.hex`: first offset changed to zero and checksum recomputed;
  integrity passes, but structural validation must reject the directory.

These fixtures test the file contract. Construction tests remain separate.
