#include "wg_algo/graph.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) return 2;
    bool reverse = false;
    auto decoder = wg_algo::Decoder::Bulk;
    for (int i = 2; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--reverse" && !reverse) reverse = true;
        else if (option == "--iterator" && decoder == wg_algo::Decoder::Bulk)
            decoder = wg_algo::Decoder::Iterator;
        else return 2;
    }
    auto graph = wg_algo::Graph::load(argv[1], decoder);
    std::vector<int> row;
    for (int i = 0; i < graph.n(); ++i) {
        const int u = reverse ? graph.n() - 1 - i : i;
        graph.decode_row(u, row);
        for (int v : row) std::cout << u << ' ' << v << '\n';
    }
}
