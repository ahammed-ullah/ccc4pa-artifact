#include "cc_con/bound.hpp"
#include "cc_con/cgf.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace {

using Clock = chrono::steady_clock;

double elapsed_seconds(Clock::time_point start) {
    return chrono::duration<double>(Clock::now() - start).count();
}

string lower(string value) {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return (char)tolower(c);
    });
    return value;
}

void print_usage(ostream& output, const char* program) {
    output << "Usage:\n"
           << "  " << program
           << " <linear|local-mis|local-color> --graph <graph.cgf> [--truss]\n"
           << "  " << program << " --list-methods\n"
           << "  " << program << " --help\n";
}

cc_con::BoundMethod parse_method(const string& name) {
    if (name == "linear") return cc_con::BoundMethod::Linear;
    if (name == "local-mis") return cc_con::BoundMethod::LocalMis;
    if (name == "local-color") return cc_con::BoundMethod::LocalColor;
    throw runtime_error("Unknown lower-bound method '" + name + "'");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && string(argv[1]) == "--help") {
            print_usage(cout, argv[0]);
            return 0;
        }
        if (argc == 2 && string(argv[1]) == "--list-methods") {
            cout << "linear\nlocal-mis\nlocal-color\n";
            return 0;
        }
        if (argc < 4) {
            print_usage(cerr, argv[0]);
            return 2;
        }

        string method_name = lower(argv[1]);
        cc_con::BoundOptions options;
        options.method = parse_method(method_name);

        filesystem::path graph_path;
        for (int i = 2; i < argc; ++i) {
            string argument = argv[i];
            if (argument == "--truss") {
                if (options.exact_truss)
                    throw runtime_error("--truss was provided more than once");
                options.exact_truss = true;
            } else if (argument == "--graph") {
                if (!graph_path.empty())
                    throw runtime_error("--graph was provided more than once");
                if (++i == argc)
                    throw runtime_error("--graph requires a CGF path");
                graph_path = argv[i];
            } else {
                throw runtime_error("Unknown argument '" + argument + "'");
            }
        }
        if (graph_path.empty())
            throw runtime_error("A CGF path is required with --graph");
        if (lower(graph_path.extension().string()) != ".cgf")
            throw runtime_error("cc_bound accepts only .cgf graph files");
        if (options.method == cc_con::BoundMethod::Linear &&
            options.exact_truss) {
            throw runtime_error("--truss is not available with the linear method");
        }

        vector<vector<int>> adj;
        Clock::time_point read_start = Clock::now();
        cc_con::CgfArtifactInfo artifact = cc_con::read_cgf(graph_path, adj);
        double read_seconds = elapsed_seconds(read_start);

        cc_con::BoundResult result;
        cc_con::compute_clique_cover_lower_bounds(adj, options, result);

        cc_con::BoundReport report;
        report.method = cc_con::bound_method_name(options.method);
        report.exact_truss = options.exact_truss;
        report.graph_name = graph_path.stem().string();
        report.graph_path = graph_path.string();
        report.n = artifact.n;
        report.m = artifact.m;
        report.cgf_bytes = artifact.file_bytes;
        report.graph_read_seconds = read_seconds;
        report.result = std::move(result);

        filesystem::path report_path = cc_con::write_bound_report(report);
        cout << "completed: method=" << report.method
             << " truss=" << (report.exact_truss ? "true" : "false")
             << " graph=" << report.graph_name
             << " lower_bound="
             << report.result.lower_bounds.at("combined_best")
             << " report=" << report_path.string() << '\n';
        return 0;
    } catch (const exception& error) {
        cerr << "cc_bound: " << error.what() << '\n';
        return 1;
    }
}
