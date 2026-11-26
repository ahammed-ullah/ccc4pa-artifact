#include "cc_con/graph.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cc_con {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool next_data_line(std::istream& input, std::string& line) {
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '%') continue;
        return true;
    }
    return false;
}

}  // namespace

void load_matrix_market_graph(const Config& config,
                              const std::string& configured_name,
                              std::vector<std::vector<int>>& adj,
                              GraphMetadata& metadata) {
    const std::filesystem::path path =
        config.resolve_path(config.require_string("graph.path"));
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to open Matrix Market file '" + path.string() + "'");
    }

    std::string line;
    if (!std::getline(input, line)) throw std::runtime_error("Matrix Market file is empty");

    std::istringstream header(line);
    std::string banner;
    std::string object;
    std::string storage;
    std::string field;
    std::string symmetry;
    header >> banner >> object >> storage >> field >> symmetry;
    object = lower(object);
    storage = lower(storage);
    field = lower(field);
    symmetry = lower(symmetry);

    if (banner != "%%MatrixMarket" || object != "matrix" || storage != "coordinate") {
        throw std::runtime_error("Expected a Matrix Market coordinate matrix");
    }
    if (field != "pattern" && field != "integer" && field != "real") {
        throw std::runtime_error("Matrix Market field must be pattern, integer, or real");
    }
    if (symmetry != "general" && symmetry != "symmetric") {
        throw std::runtime_error("Matrix Market symmetry must be general or symmetric");
    }

    if (!next_data_line(input, line)) {
        throw std::runtime_error("Matrix Market file has no dimension line");
    }

    std::uint64_t rows = 0;
    std::uint64_t columns = 0;
    std::uint64_t entries = 0;
    {
        std::istringstream dimensions(line);
        if (!(dimensions >> rows >> columns >> entries) || rows != columns) {
            throw std::runtime_error("Matrix Market graph must be a square coordinate matrix");
        }
    }
    if (rows > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Matrix Market graph exceeds int vertex identifiers");
    }

    adj.assign(static_cast<std::size_t>(rows), {});
    std::uint64_t read_entries = 0;
    while (read_entries < entries && std::getline(input, line)) {
        if (line.empty() || line.front() == '%') continue;

        std::istringstream entry(line);
        std::uint64_t one_based_u = 0;
        std::uint64_t one_based_v = 0;
        if (!(entry >> one_based_u >> one_based_v)) {
            throw std::runtime_error("Malformed Matrix Market entry");
        }

        double value = 1.0;
        if (field != "pattern" && !(entry >> value)) {
            throw std::runtime_error("Matrix Market entry is missing its value");
        }
        ++read_entries;

        if (one_based_u == 0 || one_based_v == 0 ||
            one_based_u > rows || one_based_v > rows) {
            throw std::runtime_error("Matrix Market vertex index is out of range");
        }
        if (value == 0.0 || one_based_u == one_based_v) continue;

        const int u = static_cast<int>(one_based_u - 1);
        const int v = static_cast<int>(one_based_v - 1);
        adj[u].push_back(v);
        adj[v].push_back(u);
    }
    if (read_entries != entries) {
        throw std::runtime_error("Matrix Market file ended before all entries were read");
    }

    metadata.name = configured_name.empty() ? path.stem().string() : configured_name;
    metadata.source = "file";
    metadata.type = "mm";
    metadata.parameters["path"] = path.string();
}

}  // namespace cc_con
