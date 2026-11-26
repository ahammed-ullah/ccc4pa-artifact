"""Compare the C++ reader and algorithms with genuine Rust WebGraph files."""

import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
ENCODER = Path(os.environ.get(
    "WG_ENCODER", str(ROOT.parent / "wg_con/target/release/wg_encode")))
WEBGRAPH = Path(os.environ.get(
    "WEBGRAPH_BIN", str(ROOT.parent / "wg_con/bin/webgraph")))
ALGO = Path(os.environ.get("WG_ALGO_BIN", str(ROOT / "bin/wg_algo")))
CXX = os.environ.get("CXX", "c++")


def vbyte(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return result


def write_ccf(path, n, cliques):
    edges = set()
    records = []
    for clique in cliques:
        clique = sorted(clique)
        record = vbyte(len(clique))
        previous = 0
        for vertex in clique:
            record += vbyte(vertex - previous)
            previous = vertex
        records.append(record)
        for i, u in enumerate(clique):
            for v in clique[i + 1:]:
                edges.add((u, v))

    m = len(edges)
    p = len(records)
    payload_start = 160 + 8 * (p + 1)
    offsets = [payload_start]
    for record in records:
        offsets.append(offsets[-1] + len(record))
    data = bytearray(offsets[-1])
    struct.pack_into(
        "<8sIIII9Q", data, 0, b"CCF\0\r\n\x1a\n", 1, 160, 1, 0,
        n, m, p, sum(map(len, cliques)), 160, p + 1,
        payload_start, offsets[-1] - payload_start, offsets[-1],
    )
    struct.pack_into("<{}Q".format(p + 1), data, 160, *offsets)
    data[payload_start:] = b"".join(records)
    canonical = b"CC-CON-CANONICAL-GRAPH-v1\0" + struct.pack("<QQ", n, m)
    canonical += b"".join(struct.pack("<QQ", *edge) for edge in sorted(edges))
    data[96:128] = hashlib.sha256(canonical).digest()
    data[128:160] = hashlib.sha256(data).digest()
    path.write_bytes(data)
    return edges


def encode(ccf, basename, order, window=16):
    basename.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        str(ENCODER), "--ccf", str(ccf), "--basename", str(basename),
        "--order", order, "--window", str(window),
        "--max-ref-count", "unlimited",
        "--min-interval-length", "4", "--outdegrees", "gamma",
        "--references", "gamma", "--blocks", "gamma",
        "--intervals", "gamma", "--residuals", "zeta3",
    ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    subprocess.run([str(WEBGRAPH), "build", "ef", str(basename)],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    basename.with_suffix(".offsets").unlink()


def components(n, edges):
    parent = list(range(n))

    def find(u):
        while parent[u] != u:
            u = parent[u]
        return u

    for u, v in edges:
        parent[find(u)] = find(v)
    return len({find(u) for u in range(n)})


def optimum_cover_weight(n, edges, weights):
    best = sum(weights)
    for mask in range(1 << n):
        if all(mask & (1 << u) or mask & (1 << v) for u, v in edges):
            best = min(best, sum(weights[u] for u in range(n) if mask & (1 << u)))
    return best


class InteroperabilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for path in (ENCODER, WEBGRAPH, ALGO):
            if not path.is_file():
                raise RuntimeError("missing test binary: {} (build wg_con and wg_algo, "
                                   "or set WG_ENCODER and WEBGRAPH_BIN)".format(path))
        cls.temporary = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temporary.name)
        cls.dump = cls.directory / "dump_neighbors"
        subprocess.run([
            CXX, "-std=c++17", "-O2", "-I" + str(ROOT / "include"),
            str(ROOT / "tests/dump_neighbors.cpp"), str(ROOT / "src/graph.cpp"),
            "-o", str(cls.dump),
        ], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_algo(self, algorithm, graph, config=None):
        command = [str(ALGO), algorithm, str(graph)]
        if config:
            command += ["--config", str(config)]
        result = subprocess.run(command, cwd=str(self.directory), check=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                universal_newlines=True)
        report_path = Path(result.stdout.strip())
        if not report_path.is_absolute():
            report_path = self.directory / report_path
        import json
        report = json.loads(report_path.read_text())
        self.assertIn(report["parameters"]["decoder"], ("bulk", "iterator"))
        return report

    def check_graph(self, name, n, cliques, order):
        ccf = self.directory / (name + ".ccf")
        edges = write_ccf(ccf, n, cliques)
        basename = self.directory / (name + "-" + order) / name
        encode(ccf, basename, order)
        graph = basename.with_suffix(".graph")

        expected_arcs = edges | {(v, u) for u, v in edges}
        permutation = list(range(n))
        perm_path = basename.with_suffix(".perm")
        if perm_path.is_file():
            permutation = struct.unpack("<{}I".format(n), perm_path.read_bytes())
            self.assertEqual(set(permutation), set(range(n)))

        rust = subprocess.run([str(WEBGRAPH), "to", "arcs", str(basename)],
                              check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              universal_newlines=True)
        rust_arcs = [tuple(map(int, line.split())) for line in rust.stdout.splitlines()]

        def canonical_arcs(output):
            return {(permutation[int(u)], permutation[int(v)])
                    for u, v in (line.split() for line in output.splitlines())}
        self.assertEqual(canonical_arcs(rust.stdout), expected_arcs)

        rust_rows = [[] for _ in range(n)]
        for arc in rust_arcs:
            rust_rows[arc[0]].append(arc)
        reverse_expected = [arc for row in reversed(rust_rows) for arc in row]

        # Both decoders must return the same complete, sorted rows as Rust.
        # Reverse order forces independent .ef lookups and chain resolution.
        for decoder in ("bulk", "iterator"):
            flags = ["--iterator"] if decoder == "iterator" else []
            for reverse in (False, True):
                command = [str(self.dump), str(graph)] + flags
                if reverse:
                    command.append("--reverse")
                cpp = subprocess.run(command, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, universal_newlines=True)
                self.assertEqual(cpp.returncode, 0, cpp.stderr)
                cpp_arcs = [tuple(map(int, line.split()))
                            for line in cpp.stdout.splitlines()]
                expected = reverse_expected if reverse else rust_arcs
                self.assertEqual(cpp_arcs, expected,
                                 "{} decoder, reverse={}".format(decoder, reverse))
                self.assertEqual(canonical_arcs(cpp.stdout), expected_arcs)

        answer = components(n, edges)
        matching_answers = []
        for decoder in ("bulk", "iterator"):
            config = self.iterator_config() if decoder == "iterator" else None
            for algorithm in ("bfs", "dfs"):
                report = self.run_algo(algorithm, graph, config)
                self.assertEqual(report["parameters"]["decoder"], decoder)
                self.assertEqual(report["result"]["connected_components"], answer)

            maximal = self.run_algo("matching", graph, self.maximal_config(decoder))
            improved = self.run_algo("matching", graph, config)
            self.assertEqual(maximal["parameters"]["decoder"], decoder)
            self.assertEqual(improved["parameters"]["decoder"], decoder)
            maximal_size = maximal["result"]["matching_size"]
            improved_size = improved["result"]["matching_size"]
            self.assertGreaterEqual(improved_size, maximal_size)
            self.assertLessEqual(improved_size, n // 2)
            matching_answers.append((maximal_size, improved_size))
        self.assertEqual(matching_answers[0], matching_answers[1])
        return (edges, graph, *matching_answers[0])

    def iterator_config(self):
        path = self.directory / "iterator.toml"
        path.write_text('decoder = "iterator"\n')
        return path

    def maximal_config(self, decoder):
        path = self.directory / ("maximal_" + decoder + ".toml")
        path.write_text('matching_variant = "maximal"\n' +
                        ('decoder = "iterator"\n' if decoder == "iterator" else ""))
        return path

    def test_small_original_and_reordered(self):
        cliques = [(0, 4), (1, 2, 3)]
        weights = [1, 3, 4, 5, 100, 7]  # vertex 5 is isolated
        weight_file = self.directory / "weights.txt"
        weight_file.write_text("\n".join(map(str, weights)) + "\n")
        for order in ("original", "bfs-llp"):
            with self.subTest(order=order):
                edges, graph, _, _ = self.check_graph("small", 6, cliques, order)
                for decoder in ("bulk", "iterator"):
                    config = self.directory / ("weights_" + decoder + ".toml")
                    config.write_text('weights_file = "weights.txt"\n' +
                                      ('decoder = "iterator"\n' if decoder == "iterator" else ""))
                    report = self.run_algo("wvc", graph, config)
                    self.assertEqual(report["parameters"]["decoder"], decoder)
                    value = int(report["result"]["selected_weight_sum"])
                    if order == "bfs-llp":
                        permutation = struct.unpack(
                            "<6I", graph.with_suffix(".perm").read_bytes())
                        self.assertNotEqual(permutation, tuple(range(6)))
                    self.assertEqual(value, 8)  # catches incorrect weight permutation
                    self.assertLessEqual(value, 2 * optimum_cover_weight(6, edges, weights))

    def test_length_three_augmentation(self):
        # Greedy picks (0,1); Figure 6 replaces it with (0,2) and (1,3).
        edges, _, maximal, improved = self.check_graph(
            "augment", 4, [(0, 1), (0, 2), (1, 3)], "original")
        self.assertEqual(len(edges), 3)
        self.assertEqual(maximal, 1)
        self.assertEqual(improved, 2)

    def test_reference_rich_graph(self):
        # Left rows share 48 separated neighbors, which intervals cannot
        # express cheaply. Each also has one different neighbor.
        edges = [(u, 32 + 2 * k) for u in range(32) for k in range(48)]
        edges += [(u, 33 + 2 * u) for u in range(32)]
        _, graph, _, _ = self.check_graph("reference", 128, edges, "original")
        # These repeated rows should benefit from actual references. An
        # otherwise identical window-zero encoding checks the fixture itself.
        no_ref = self.directory / "reference-no-ref" / "reference"
        encode(self.directory / "reference.ccf", no_ref, "original", window=0)
        self.assertLess(graph.stat().st_size,
                        no_ref.with_suffix(".graph").stat().st_size)

    def test_ef_inventory_boundary(self):
        # The Elias--Fano select inventory starts another block at row 4096.
        edges = [(0, 1), (4094, 4104), (4095, 4100),
                 (4096, 4101), (4103, 4104)]
        self.check_graph("inventory", 4105, edges, "original")


if __name__ == "__main__":
    unittest.main()
