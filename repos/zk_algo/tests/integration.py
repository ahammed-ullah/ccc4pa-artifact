"""Check the Zuckerli reader against graphs built from independent CCF fixtures."""

import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
ENCODER = Path(os.environ.get("ZK_ENCODER", str(ROOT.parent / "zk_con/bin/zk_con")))
ALGO = Path(os.environ.get("ZK_ALGO_BIN", str(ROOT / "bin/zk_algo")))
CHECK = Path(os.environ.get("ZK_CHECK_BIN", str(ROOT / "bin/zk_check")))
MVM_CHECK = Path(os.environ.get("ZK_MVM_CHECK_BIN", str(ROOT / "bin/zk_mvm_check")))
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
        clique = sorted(set(clique))
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


def components(n, edges):
    parent = list(range(n))

    def find(u):
        while parent[u] != u:
            u = parent[u]
        return u

    for u, v in edges:
        parent[find(u)] = find(v)
    return len({find(u) for u in range(n)})


def weighted_cover_value(neighbors, weights):
    residual = list(weights)
    total = 0
    for u, row in enumerate(neighbors):
        if residual[u] == 0:
            continue
        for v in row:
            if v <= u or residual[v] == 0:
                continue
            delta = min(residual[u], residual[v])
            residual[u] -= delta
            residual[v] -= delta
            if residual[u] == 0:
                total += weights[u]
            if residual[v] == 0:
                total += weights[v]
            if residual[u] == 0:
                break
    return total


class IntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for path in (ENCODER, ALGO, CHECK, MVM_CHECK):
            if not path.is_file():
                raise RuntimeError("missing test binary: {} (build zk_con and zk_algo)".format(path))
        cls.temporary = tempfile.TemporaryDirectory(prefix="zk-algo-test-")
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

    def run_algo(self, algorithm, graph, config=None, env=None):
        command = [str(ALGO), algorithm, str(graph)]
        if config is not None:
            command += ["--config", str(config)]
        if env is None:
            env = dict(os.environ)
            env.pop("MVM_SEED", None)
        output = subprocess.run(command, cwd=str(self.directory), check=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                universal_newlines=True, env=env)
        return json.loads(Path(output.stdout.strip()).read_text())

    def check_graph(self, name, n, cliques, permutation=None):
        ccf = self.directory / (name + ".ccf")
        edges = write_ccf(ccf, n, cliques)
        base = self.directory / name
        command = [str(ENCODER), "encode", str(ccf), str(base)]
        if permutation is not None:
            perm_path = base.with_suffix(".perm")
            perm_path.write_bytes(struct.pack("<{}I".format(n), *permutation))
            command += ["--perm", str(perm_path)]
        subprocess.run(command, check=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
        graph = base.with_suffix(".zk")
        subprocess.run([str(CHECK), str(graph), str(ccf)], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        perm = permutation if permutation is not None else list(range(n))
        inverse = {old: new for new, old in enumerate(perm)}
        neighbors = [[] for _ in range(n)]
        for u, v in edges:
            a, b = inverse[u], inverse[v]
            neighbors[a].append(b)
            neighbors[b].append(a)
        for row in neighbors:
            row.sort()

        for reverse in (False, True):
            command = [str(self.dump), str(graph)]
            if reverse:
                command.append("--reverse")
            result = subprocess.run(command, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, universal_newlines=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            vertices = range(n - 1, -1, -1) if reverse else range(n)
            expected = [(u, v) for u in vertices for v in neighbors[u]]
            actual = [tuple(map(int, line.split()))
                      for line in result.stdout.splitlines()]
            self.assertEqual(actual, expected)

        for algorithm in ("bfs", "dfs"):
            report = self.run_algo(algorithm, graph)
            self.assertEqual(report["result"]["connected_components"],
                             components(n, edges))
            self.assertEqual(report["representation"], "zuckerli")
            self.assertEqual(report["input"]["bundle_bytes"],
                             graph.stat().st_size + base.with_suffix(".zki").stat().st_size
                             + (4 * n if permutation is not None else 0))
            self.assertIn("zuckerli_load", report["timing_seconds"])
            self.assertIn("algorithm", report["timing_seconds"])
            self.assertIn("process_peak_rss_bytes", report["memory"])
        return graph, neighbors, edges

    def test_small_original_and_reordered(self):
        cliques = [(0, 4), (1, 2, 3)]  # vertex 5 is isolated
        weights = [1, 3, 4, 5, 100, 7]
        weight_file = self.directory / "weights.txt"
        weight_file.write_text("\n".join(map(str, weights)) + "\n")
        config = self.directory / "weights.toml"
        config.write_text('weights_file = "weights.txt"\n')
        mvm_config = self.directory / "mvm.toml"
        mvm_config.write_text('weights_file = "weights.txt"\n'
                              'mvm_epsilon = 0.01\n'
                              'mvm_seed = 1\n')
        for name, permutation in (("small-original", None),
                                  ("small-reordered", [4, 2, 1, 0, 3, 5])):
            with self.subTest(name=name):
                graph, neighbors, _ = self.check_graph(
                    name, 6, cliques, permutation)
                report = self.run_algo("wvc", graph, config)
                perm = permutation if permutation is not None else list(range(6))
                encoded_weights = [weights[old] for old in perm]
                self.assertEqual(int(report["result"]["selected_weight_sum"]),
                                 weighted_cover_value(neighbors, encoded_weights))

                oracle_command = [str(MVM_CHECK), str(graph), str(weight_file)]
                if permutation is not None:
                    oracle_command.append(str(graph.with_suffix(".perm")))
                oracle = subprocess.run(oracle_command, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, universal_newlines=True)
                self.assertEqual(oracle.returncode, 0, oracle.stderr)
                expected_weight, expected_samples = map(int, oracle.stdout.split())
                mvm = self.run_algo("mvm", graph, mvm_config)
                self.assertEqual(int(mvm["result"]["matched_vertex_weight_sum"]),
                                 expected_weight)
                self.assertEqual(mvm["parameters"]["mvm_sample_limit"],
                                 expected_samples)
                self.assertEqual(mvm["mvm_execution"]["status"], "complete")
                self.assertEqual(mvm["mvm_execution"]["stop_reason"],
                                 "sample_limit")
                self.assertEqual(mvm["mvm_execution"]["samples_completed"],
                                 expected_samples)
                self.assertEqual(mvm["parameters"]["mvm_seed"], 1)
                self.assertIn("weight_preparation", mvm["timing_seconds"])

                alternate_env = dict(os.environ, MVM_SEED="2")
                alternate = self.run_algo("mvm", graph, mvm_config, alternate_env)
                self.assertEqual(alternate["parameters"]["mvm_seed"], 2)

    def test_matching_augmentation(self):
        graph, _, _ = self.check_graph(
            "augment", 4, [(0, 1), (0, 2), (1, 3)])
        config = self.directory / "maximal.toml"
        config.write_text('matching_variant = "maximal"\n')
        maximal = self.run_algo("matching", graph, config)
        improved = self.run_algo("matching", graph)
        self.assertEqual(maximal["result"]["matching_size"], 1)
        self.assertEqual(improved["result"]["matching_size"], 2)

    def test_mvm_128_bit_result(self):
        graph, _, _ = self.check_graph("mvm-heavy-edge", 2, [(0, 1)])
        weight_file = self.directory / "heavy-weights.txt"
        weight_file.write_text("18446744073709551615\n18446744073709551615\n")
        config = self.directory / "heavy-mvm.toml"
        config.write_text('weights_file = "heavy-weights.txt"\n')
        report = self.run_algo("mvm", graph, config)
        self.assertEqual(report["result"]["matched_vertex_weight_sum"],
                         "36893488147419103230")
        self.assertEqual(report["mvm_execution"]["status"], "complete")
        self.assertEqual(report["mvm_execution"]["stop_reason"],
                         "all_vertices_matched")
        self.assertEqual(report["mvm_execution"]["samples_completed"], 1)
        self.assertGreater(report["parameters"]["mvm_sample_limit"], 1)

    def test_mvm_four_edge_augmentation(self):
        # Seed 1 first samples 3, 2, 0, 1. The first two samples match
        # 3--4 and 1--2; center 0 cannot improve; center 1 then replaces
        # both matching edges along 0--1--2--4--3.
        graph, _, _ = self.check_graph(
            "mvm-four-edge", 5, [(0, 1), (1, 2), (2, 4), (4, 3)])
        weight_file = self.directory / "four-edge-weights.txt"
        weight_file.write_text("5\n6\n10\n1\n7\n")
        config = self.directory / "four-edge-mvm.toml"
        config.write_text('weights_file = "four-edge-weights.txt"\n'
                          'mvm_seed = 1\n')
        oracle = subprocess.run([str(MVM_CHECK), str(graph), str(weight_file)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                universal_newlines=True)
        self.assertEqual(oracle.returncode, 0, oracle.stderr)
        report = self.run_algo("mvm", graph, config)
        self.assertEqual(report["result"]["matched_vertex_weight_sum"], "28")
        self.assertEqual(report["mvm_execution"]["positive_augmentations"], 3)

    def test_reference_rich_and_empty(self):
        cliques = [list(range(20)), list(range(10, 30)),
                   list(range(30, 50))]
        cliques += [(u, u + 8) for u in range(40, 56)]
        self.check_graph("overlap", 64, cliques)
        empty, _, _ = self.check_graph("empty", 4, [])
        empty_mvm = self.run_algo("mvm", empty)
        self.assertEqual(empty_mvm["result"]["matched_vertex_weight_sum"], "0")
        self.assertEqual(empty_mvm["mvm_execution"]["positive_augmentations"], 0)
        self.check_graph("long-chain", 260, [list(range(260))])


if __name__ == "__main__":
    unittest.main()
