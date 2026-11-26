#include "fm_con/fm.hpp"
#include "fm_con/report.hpp"

#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace std;

namespace {

void print_usage(ostream& output, const char* program) {
    output << "Usage:\n"
           << "  " << program
           << " --graph <graph.cgf> [--delta <value>]"
           << " [--workspace <linear | prefix-tree>]\n"
           << "  " << program << " --help\n";
}

long double parse_delta(const string& text) {
    if (text.empty())
        throw runtime_error("--delta requires a value");
    for (unsigned char c : text) {
        if (!isdigit(c) && c != '.' && c != '+' && c != '-' &&
            c != 'e' && c != 'E') {
            throw runtime_error(
                "--delta must be a decimal number with 0 < delta < 1");
        }
    }

    size_t parsed = 0;
    long double delta;
    try {
        delta = stold(text, &parsed);
    } catch (const exception&) {
        throw runtime_error(
            "--delta must be a decimal number with 0 < delta < 1");
    }
    if (parsed != text.size() || !isfinite(delta) ||
        !(delta > 0.0L && delta < 1.0L)) {
        throw runtime_error(
            "--delta must be a decimal number with 0 < delta < 1");
    }
    return delta;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        filesystem::path graph_path;
        fm_con::FmOptions options;
        for (int i = 1; i < argc; ++i) {
            string argument = argv[i];
            if (argument == "--help") {
                print_usage(cout, argv[0]);
                return 0;
            }
            if (argument == "--graph") {
                if (++i == argc)
                    throw runtime_error("--graph requires a CGF path");
                graph_path = argv[i];
                continue;
            }
            if (argument == "--workspace") {
                if (++i == argc)
                    throw runtime_error("--workspace requires a method");
                string method = argv[i];
                if (method == "linear") {
                    options.workspace = fm_con::WorkspaceMethod::Linear;
                } else if (method == "prefix-tree") {
                    options.workspace = fm_con::WorkspaceMethod::PrefixTree;
                } else {
                    throw runtime_error(
                        "--workspace must be linear or prefix-tree");
                }
                continue;
            }
            if (argument == "--delta") {
                if (++i == argc)
                    throw runtime_error("--delta requires a value");
                options.delta_argument = argv[i];
                options.delta = parse_delta(options.delta_argument);
                options.fixed_delta = true;
                continue;
            }
            throw runtime_error("Unknown argument '" + argument + "'");
        }

        if (graph_path.empty()) {
            print_usage(cerr, argv[0]);
            return 1;
        }
        if (graph_path.extension() != ".cgf")
            throw runtime_error("FM input must have the .cgf extension");

        // Reserve the report name before any measured FM work. Independent
        // repetitions can then use the same graph, delta, and directory safely.
        fm_con::ReportSlot report_slot(graph_path, options);
        fm_con::FmResult result = fm_con::construct_fm(graph_path, options);
        report_slot.publish(result);
        cout << "completed: algorithm=fm workspace="
             << fm_con::workspace_method_name(result.workspace)
             << " graph=" << graph_path.stem().string();
        if (result.fixed_delta)
            cout << " delta=" << result.delta_argument;
        cout << " report=" << report_slot.path().string() << '\n';
        return 0;
    } catch (const exception& error) {
        cerr << "fm_con: " << error.what() << '\n';
        return 1;
    }
}
