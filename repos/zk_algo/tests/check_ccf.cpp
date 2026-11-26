// Untimed whole-graph check against the clique cover used by zk_con.
#include "zk_algo/graph.hpp"
#include "ccf_graph.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace std;

int main(int argc, char** argv) {
    if (argc != 3) {
        cerr << "Usage: zk_check graph.zk graph.ccf\n";
        return 2;
    }
    try {
        const filesystem::path zk_path(argv[1]);
        auto perm_path = zk_path;
        perm_path.replace_extension(".perm");
        const auto graph = zk_algo::Graph::load(zk_path.string());
        const zk::CcfGraph cover(argv[2],
                                 filesystem::exists(perm_path)
                                     ? perm_path.string() : "");
        if (graph.n() != static_cast<int>(cover.n()) ||
            graph.arcs() != 2 * cover.m()) {
            throw runtime_error("Zuckerli and CCF graph counts differ");
        }

        vector<int> decoded;
        vector<uint32_t> expected;
        for (int u = 0; u < graph.n(); ++u) {
            graph.decode_row(u, decoded);
            cover.neighbors(static_cast<uint32_t>(u), expected);
            if (decoded.size() != expected.size()) {
                throw runtime_error("degree differs at vertex " + to_string(u));
            }
            for (size_t i = 0; i < expected.size(); ++i) {
                if (decoded[i] != static_cast<int>(expected[i])) {
                    throw runtime_error("neighbor differs at vertex " +
                                        to_string(u) + ", position " +
                                        to_string(i));
                }
            }
        }
        cout << "Validated " << graph.n() << " neighborhoods\n";
    } catch (const exception& error) {
        cerr << "zk_check: " << error.what() << '\n';
        return 1;
    }
}
