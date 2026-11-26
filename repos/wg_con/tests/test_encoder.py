"""Check that WebGraph decodes both vertex orders back to the CCF graph."""

import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
ENCODER = Path(os.environ.get("WG_ENCODER", str(ROOT / "target/release/wg_encode")))
WEBGRAPH = Path(os.environ.get("WEBGRAPH_BIN", str(ROOT / "bin/webgraph")))
EDGES = {(0, 4), (1, 2), (1, 3), (2, 3)}
ARCS = EDGES | {(v, u) for u, v in EDGES}


def cover_file(path):
    # Two cliques, {0,4} and {1,2,3}. BFS visits 4 before 1, so the
    # reordered path has to translate vertex IDs and sort the new neighbors.
    data = bytearray(191)
    struct.pack_into(
        "<8sIIII9Q", data, 0, b"CCF\0\r\n\x1a\n", 1, 160, 1, 0,
        5, 4, 2, 5, 160, 3, 184, 7, 191,
    )
    struct.pack_into("<3Q", data, 160, 184, 187, 191)
    data[184:191] = bytes((2, 0, 4, 3, 1, 1, 1))
    graph = b"CC-CON-CANONICAL-GRAPH-v1\0" + struct.pack("<QQ", 5, 4)
    graph += b"".join(struct.pack("<QQ", *edge) for edge in sorted(EDGES))
    data[96:128] = hashlib.sha256(graph).digest()
    data[128:160] = hashlib.sha256(data).digest()
    path.write_bytes(data)


@unittest.skipUnless(ENCODER.is_file() and WEBGRAPH.is_file(),
                     "make builds the encoder and WebGraph CLI for this test")
class EncoderInteroperabilityTests(unittest.TestCase):
    def test_original_and_bfs_llp_decode_to_same_graph(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            ccf = directory / "small.ccf"
            cover_file(ccf)
            for order in ("original", "bfs-llp"):
                basename = directory / order / "small"
                basename.parent.mkdir()
                command = [
                    str(ENCODER), "--ccf", str(ccf), "--basename", str(basename),
                    "--order", order, "--window", "16", "--max-ref-count", "unlimited",
                    "--min-interval-length", "4", "--outdegrees", "gamma",
                    "--references", "gamma", "--blocks", "gamma",
                    "--intervals", "gamma", "--residuals", "zeta3",
                ]
                subprocess.run(command, check=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
                subprocess.run([str(WEBGRAPH), "build", "ef", str(basename)],
                               check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                basename.with_suffix(".offsets").unlink()
                result = subprocess.run(
                    [str(WEBGRAPH), "to", "arcs", str(basename)], check=True,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True,
                )
                permutation = tuple(range(5))
                if order == "bfs-llp":
                    raw = basename.with_suffix(".perm").read_bytes()
                    permutation = struct.unpack("<5I", raw)
                    self.assertEqual(set(permutation), set(range(5)))
                    self.assertNotEqual(permutation, tuple(range(5)))
                arcs = {
                    (permutation[int(u)], permutation[int(v)])
                    for u, v in (line.split() for line in result.stdout.splitlines())
                }
                self.assertEqual(arcs, ARCS)


if __name__ == "__main__":
    unittest.main()
