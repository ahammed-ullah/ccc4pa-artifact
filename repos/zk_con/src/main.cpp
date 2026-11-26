#include "ccf_graph.hpp"
#include "encode.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace std;

namespace {

void usage(const char* program) {
    cerr << "Usage:\n  " << program
         << " encode graph.ccf output_basename [--perm order.perm]\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 4 || argc > 6) {
            usage(argv[0]);
            return 2;
        }
        const string action = argv[1];
        if (action != "encode") {
            usage(argv[0]);
            return 2;
        }
        string perm;
        if (argc == 6) {
            if (string(argv[4]) != "--perm") {
                usage(argv[0]);
                return 2;
            }
            perm = argv[5];
        } else if (argc == 5) {
            usage(argv[0]);
            return 2;
        }

        const auto start = chrono::steady_clock::now();
        zk::CcfGraph source(argv[2], perm);
        const zk::EncodeStats stats = zk::encode_graph(source, argv[3]);
        const double elapsed = chrono::duration<double>(
            chrono::steady_clock::now() - start).count();
        cout << "{\"action\":\"encode\",\"n\":" << stats.n
             << ",\"m\":" << stats.m
             << ",\"graph_bytes\":" << stats.graph_bytes
             << ",\"index_bytes\":" << stats.index_bytes
             << ",\"selected_references\":" << stats.selected_references
             << ",\"seconds\":" << elapsed << "}\n";
        return 0;
    } catch (const exception& error) {
        cerr << "zk_con: " << error.what() << '\n';
        return 1;
    }
}
