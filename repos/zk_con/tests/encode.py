#!/usr/bin/env python3
"""Encoder checks with small CCF files assembled independently here."""

import hashlib
import json
import pathlib
import random
import struct
import subprocess
import sys
import tempfile


def vbyte(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def ccf(path, n, cliques):
    cliques = [sorted(set(group)) for group in cliques]
    edges = set()
    records = []
    for group in cliques:
        assert len(group) >= 2 and 0 <= group[0] < group[-1] < n
        record = bytearray(vbyte(len(group)))
        last = 0
        for vertex in group:
            record.extend(vbyte(vertex - last))
            last = vertex
        records.append(bytes(record))
        for i, u in enumerate(group):
            for v in group[i + 1:]:
                edges.add((u, v))

    start = 160 + 8 * (len(records) + 1)
    offsets = [start]
    for record in records:
        offsets.append(offsets[-1] + len(record))
    header = bytearray(160)
    header[:8] = b"CCF\0\r\n\x1a\n"
    struct.pack_into("<IIII", header, 8, 1, 160, 1, 0)
    struct.pack_into("<QQQQQQQQQ", header, 24, n, len(edges), len(records),
                     sum(map(len, cliques)), 160, len(offsets), start,
                     offsets[-1] - start, offsets[-1])
    canonical = b"".join(struct.pack("<II", u, v) for u, v in sorted(edges))
    header[96:128] = hashlib.sha256(canonical).digest()
    header[128:160] = hashlib.sha256(b"".join(records)).digest()
    path.write_bytes(header + b"".join(struct.pack("<Q", x) for x in offsets) +
                     b"".join(records))
    return len(edges)


def run(binary, source, output, perm=None):
    command = [binary, "encode", str(source), str(output)]
    if perm is not None:
        command.extend(["--perm", str(perm)])
    completed = subprocess.run(command, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, universal_newlines=True,
                               check=True)
    return json.loads(completed.stdout)


def check_bundle(source, output, n, m):
    graph = pathlib.Path(str(output) + ".zk").read_bytes()
    index = pathlib.Path(str(output) + ".zki").read_bytes()
    source_header = source.read_bytes()[:160]
    assert graph[:8] == b"ZKCCF001"
    assert struct.unpack_from("<I", graph, 8)[0] == n
    assert struct.unpack_from("<I", graph, 12)[0] == 128
    assert struct.unpack_from("<Q", graph, 16)[0] == m
    assert graph[24:32] == bytes([1, 3, 3, 1, 0, 2, 1, 1])
    assert graph[32:64] == source_header[96:128]

    assert index[:8] == b"ZKIDX001"
    assert struct.unpack_from("<I", index, 8)[0] == n
    width = index[12]
    assert 1 <= width <= 64
    assert len(index) == 24 + ((n + 1) * width + 7) // 8
    end = struct.unpack_from("<Q", index, 16)[0]
    payload = index[24:]
    offsets = []
    for u in range(n + 1):
        bit = u * width
        first = bit // 8
        last = (bit + width + 7) // 8
        chunk = int.from_bytes(payload[first:last], "little")
        offsets.append((chunk >> (bit % 8)) & ((1 << width) - 1))
    assert offsets == sorted(offsets)
    assert offsets[0] >= 64 * 8
    assert offsets[-1] == end
    assert (end + 7) // 8 == len(graph)
    if end % 8:
        assert graph[-1] >> (end % 8) == 0


def main():
    binary = str(pathlib.Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="zk-con-test-") as directory:
        root = pathlib.Path(directory)
        cases = [
            ("zero-vertex", 0, []),
            ("empty", 4, []),
            ("edge", 2, [[0, 1]]),
            ("overlap", 150, [list(range(20)), list(range(5, 15)),
                              list(range(30, 40))] +
             [[i, i + 1] for i in range(40, 120)] +
             [[i, i + 2] for i in range(40, 110)]),
            ("long-chain", 260, [list(range(260))]),
        ]
        rng = random.Random(1909)
        for case in range(8):
            n = 45 + case * 3
            groups = []
            for _ in range(30):
                size = rng.randrange(2, min(16, n) + 1)
                start = rng.randrange(n - size + 1)
                groups.append(list(range(start, start + size)))
            cases.append(("random-%02d" % case, n, groups))
        for name, n, cliques in cases:
            source = root / (name + ".ccf")
            m = ccf(source, n, cliques)
            orders = [("original", None)]
            if n > 2:
                vertices = list(range(n))
                random.Random(19).shuffle(vertices)
                perm = root / (name + ".perm")
                perm.write_bytes(b"".join(struct.pack("<I", x) for x in vertices))
                orders.append(("reordered", perm))
            for label, perm in orders:
                output = root / (name + "-" + label)
                encoded = run(binary, source, output, perm)
                assert encoded["n"] == n
                assert encoded["m"] == m
                assert encoded["graph_bytes"] == pathlib.Path(str(output) + ".zk").stat().st_size
                assert encoded["index_bytes"] == pathlib.Path(str(output) + ".zki").stat().st_size
                check_bundle(source, output, n, m)
                again = root / (name + "-" + label + "-again")
                repeated = run(binary, source, again, perm)
                assert repeated["selected_references"] == encoded["selected_references"]
                assert pathlib.Path(str(again) + ".zk").read_bytes() == pathlib.Path(str(output) + ".zk").read_bytes()
                assert pathlib.Path(str(again) + ".zki").read_bytes() == pathlib.Path(str(output) + ".zki").read_bytes()
                if name == "overlap":
                    assert encoded["selected_references"] > 0
        print("CCF/Zuckerli encoder format and determinism checks passed")


if __name__ == "__main__":
    main()
