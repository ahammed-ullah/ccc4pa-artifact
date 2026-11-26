#include "zk_algo/graph.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) return 2;
    if (argc == 3 && string(argv[2]) != "--reverse") return 2;
    try {
        const auto graph = zk_algo::Graph::load(argv[1]);
        vector<int> row;
        for (int i = 0; i < graph.n(); ++i) {
            const int u = argc == 3 ? graph.n() - 1 - i : i;
            graph.decode_row(u, row);
            for (int v : row) cout << u << ' ' << v << '\n';
        }
    } catch (const exception& error) {
        cerr << error.what() << '\n';
        return 1;
    }
}
