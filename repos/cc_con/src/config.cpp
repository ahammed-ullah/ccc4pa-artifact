#include "cc_con/config.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace cc_con {
namespace {

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (first >= last) return {};
    return std::string(first, last);
}

std::string strip_comment(const std::string& line) {
    // A '#' inside a quoted path/name is data, not the start of a comment.
    bool quoted = false;
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quoted && c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            quoted = !quoted;
        } else if (c == '#' && !quoted) {
            return line.substr(0, i);
        }
    }
    return line;
}

std::size_t find_assignment(const std::string& line) {
    // Apply the same quote-aware scan to '=' so quoted values remain intact.
    bool quoted = false;
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quoted && c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') quoted = !quoted;
        if (c == '=' && !quoted) return i;
    }
    return std::string::npos;
}

bool valid_name(const std::string& value, bool dotted) {
    if (value.empty()) return false;
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '_' || c == '-') continue;
        if (dotted && c == '.') continue;
        return false;
    }
    return true;
}

std::string parse_quoted(const std::string& raw, std::size_t line_number) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') {
        throw std::runtime_error("Invalid quoted string at line " +
                                 std::to_string(line_number));
    }

    std::string out;
    out.reserve(raw.size() - 2);
    for (std::size_t i = 1; i + 1 < raw.size(); ++i) {
        const char c = raw[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (++i + 1 > raw.size()) {
            throw std::runtime_error("Invalid string escape at line " +
                                     std::to_string(line_number));
        }
        switch (raw[i]) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default:
                throw std::runtime_error("Unsupported string escape at line " +
                                         std::to_string(line_number));
        }
    }
    return out;
}

ConfigValue parse_value(const std::string& raw, std::size_t line_number) {
    // Keep the accepted language intentionally small and unambiguous. A token is
    // tried as integer before floating point so typed access can distinguish them.
    if (raw.empty()) {
        throw std::runtime_error("Missing value at line " + std::to_string(line_number));
    }
    if (raw.front() == '"') return parse_quoted(raw, line_number);
    if (raw == "true") return true;
    if (raw == "false") return false;

    errno = 0;
    char* end = nullptr;
    const long long integer = std::strtoll(raw.c_str(), &end, 10);
    if (errno == 0 && end != raw.c_str() && *end == '\0') {
        return static_cast<std::int64_t>(integer);
    }

    errno = 0;
    end = nullptr;
    const double number = std::strtod(raw.c_str(), &end);
    if (errno == 0 && end != raw.c_str() && *end == '\0') return number;

    throw std::runtime_error("Unsupported value at line " + std::to_string(line_number) +
                             ": " + raw);
}

template <typename T>
const T& expect_type(const ConfigValue& value, const std::string& key, const char* type_name) {
    const T* typed = std::get_if<T>(&value);
    if (!typed) {
        throw std::runtime_error("Configuration key '" + key + "' must be " + type_name);
    }
    return *typed;
}

}  // namespace

Config Config::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to open configuration file '" + path.string() + "'");
    }

    Config config;
    config.source_path_ = std::filesystem::absolute(path).lexically_normal();

    // Flatten [section] plus key into a dotted map entry (for example,
    // graph.parameters.n). Modules therefore need no parser-specific tree API.
    std::string section;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(strip_comment(line));
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.back() != ']') {
                throw std::runtime_error("Malformed section at line " +
                                         std::to_string(line_number));
            }
            section = trim(line.substr(1, line.size() - 2));
            if (!valid_name(section, true)) {
                throw std::runtime_error("Invalid section name at line " +
                                         std::to_string(line_number));
            }
            continue;
        }

        const std::size_t equals = find_assignment(line);
        if (equals == std::string::npos) {
            throw std::runtime_error("Expected key = value at line " +
                                     std::to_string(line_number));
        }

        const std::string key = trim(line.substr(0, equals));
        const std::string raw_value = trim(line.substr(equals + 1));
        if (!valid_name(key, false)) {
            throw std::runtime_error("Invalid key at line " + std::to_string(line_number));
        }

        const std::string full_key = section.empty() ? key : section + "." + key;
        if (config.values_.count(full_key)) {
            throw std::runtime_error("Duplicate configuration key '" + full_key + "'");
        }
        config.values_.emplace(full_key, parse_value(raw_value, line_number));
    }

    return config;
}

bool Config::has(const std::string& key) const {
    return values_.find(key) != values_.end();
}

const ConfigValue& Config::consume(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        throw std::runtime_error("Missing required configuration key '" + key + "'");
    }
    // Consumption is observable only through unused_keys(); access remains
    // logically const so independent option parsers can share one Config.
    consumed_.insert(key);
    return it->second;
}

std::string Config::require_string(const std::string& key) const {
    return expect_type<std::string>(consume(key), key, "a quoted string");
}

std::string Config::string_or(const std::string& key, const std::string& fallback) const {
    return has(key) ? require_string(key) : fallback;
}

std::int64_t Config::require_integer(const std::string& key) const {
    return expect_type<std::int64_t>(consume(key), key, "an integer");
}

std::int64_t Config::integer_or(const std::string& key, const std::int64_t fallback) const {
    return has(key) ? require_integer(key) : fallback;
}

double Config::require_number(const std::string& key) const {
    const ConfigValue& value = consume(key);
    if (const auto* number = std::get_if<double>(&value)) return *number;
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
    }
    throw std::runtime_error("Configuration key '" + key + "' must be numeric");
}

double Config::number_or(const std::string& key, const double fallback) const {
    return has(key) ? require_number(key) : fallback;
}

bool Config::bool_or(const std::string& key, const bool fallback) const {
    return has(key) ? expect_type<bool>(consume(key), key, "a boolean") : fallback;
}

std::filesystem::path Config::resolve_path(const std::string& value) const {
    std::filesystem::path path(value);
    if (path.is_absolute()) return path.lexically_normal();
    return (source_path_.parent_path() / path).lexically_normal();
}

std::vector<std::string> Config::unused_keys() const {
    std::vector<std::string> unused;
    for (const auto& [key, value] : values_) {
        (void)value;
        if (!consumed_.count(key)) unused.push_back(key);
    }
    return unused;
}

CommonOptions parse_common_options(const Config& config) {
    // These stages wrap every construction and are parsed before algorithm timing.
    CommonOptions options;
    options.assignment_minimal =
        config.bool_or("postprocess.assignment_minimal", false);
    options.verification = config.bool_or("verification.enabled", false);

    options.write_cover = config.bool_or("output.write_cover", false);
    if (options.write_cover) {
        if (!config.has("output.directory")) {
            throw std::runtime_error(
                "output.write_cover = true requires output.directory");
        }
        const std::string directory = config.require_string("output.directory");
        if (directory.empty()) {
            throw std::runtime_error("output.directory cannot be empty");
        }
        options.cover_directory = config.resolve_path(directory);
    } else if (config.has("output.directory")) {
        throw std::runtime_error(
            "output.directory requires output.write_cover = true");
    }

    return options;
}

}  // namespace cc_con
