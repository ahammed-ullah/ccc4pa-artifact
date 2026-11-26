#include "zk_algo/config.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

using namespace std;

namespace zk_algo {
namespace {

string trim(const string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

string without_comment(const string& text) {
    bool quoted = false;
    bool escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (escaped) { escaped = false; continue; }
        if (quoted && text[i] == '\\') { escaped = true; continue; }
        if (text[i] == '"') quoted = !quoted;
        if (text[i] == '#' && !quoted) return text.substr(0, i);
    }
    return text;
}

string quoted_string(const string& raw, size_t line) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"')
        throw runtime_error("config line " + to_string(line) +
                            ": expected a quoted TOML string");
    string result;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        if (raw[i] != '\\') { result.push_back(raw[i]); continue; }
        if (++i + 1 >= raw.size())
            throw runtime_error("config line " + to_string(line) +
                                ": incomplete string escape");
        switch (raw[i]) {
            case '\\': result.push_back('\\'); break;
            case '"': result.push_back('"'); break;
            case 'n': result.push_back('\n'); break;
            case 't': result.push_back('\t'); break;
            default:
                throw runtime_error("config line " + to_string(line) +
                                    ": unsupported string escape");
        }
    }
    return result;
}

}  // namespace

Config load_config(const filesystem::path& path) {
    ifstream file(path);
    if (!file) throw runtime_error("cannot open config: " + path.string());
    Config config;
    config.source_path = filesystem::absolute(path).lexically_normal();
    unordered_set<string> seen;
    string line;
    size_t number = 0;
    while (getline(file, line)) {
        ++number;
        line = trim(without_comment(line));
        if (line.empty()) continue;
        const auto equal = line.find('=');
        if (equal == string::npos)
            throw runtime_error("config line " + to_string(number) +
                                ": expected key = value");
        const auto key = trim(line.substr(0, equal));
        const auto raw = trim(line.substr(equal + 1));
        if (!seen.insert(key).second)
            throw runtime_error("duplicate config key: " + key);
        if (key == "matching_variant") {
            const auto value = quoted_string(raw, number);
            config.matching_variant_specified = true;
            if (value == "three_halves") config.matching_variant = MatchingVariant::ThreeHalves;
            else if (value == "maximal") config.matching_variant = MatchingVariant::Maximal;
            else throw runtime_error("matching_variant must be three_halves or maximal");
        } else if (key == "weights_file") {
            const auto value = quoted_string(raw, number);
            filesystem::path weight_path(value);
            if (weight_path.is_relative())
                weight_path = config.source_path->parent_path() / weight_path;
            config.weights_file = filesystem::absolute(weight_path).lexically_normal();
        } else if (key == "mvm_epsilon") {
            size_t consumed = 0;
            try { config.mvm_epsilon = stod(raw, &consumed); }
            catch (const exception&) {
                throw runtime_error("mvm_epsilon must be a positive finite number");
            }
            if (consumed != raw.size() || !isfinite(config.mvm_epsilon) ||
                config.mvm_epsilon <= 0)
                throw runtime_error("mvm_epsilon must be a positive finite number");
            config.mvm_epsilon_specified = true;
        } else if (key == "mvm_seed") {
            if (raw.empty() || !all_of(raw.begin(), raw.end(), [](unsigned char ch) {
                return isdigit(ch) != 0;
            })) throw runtime_error("mvm_seed must be a uint64 integer");
            size_t consumed = 0;
            try { config.mvm_seed = stoull(raw, &consumed); }
            catch (const exception&) {
                throw runtime_error("mvm_seed must be a uint64 integer");
            }
            if (consumed != raw.size())
                throw runtime_error("mvm_seed must be a uint64 integer");
            config.mvm_seed_specified = true;
        } else {
            throw runtime_error("unknown config key: " + key);
        }
    }
    return config;
}

}  // namespace zk_algo
