#include "graph_metadata_io.hpp"

#include "artifact_support.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace cc_con {
namespace {

constexpr std::int64_t kPreparationSchemaVersion = 1;

struct JsonValue {
    bool object = false;
    std::map<std::string, JsonValue> members;
    ConfigValue scalar;
};

class JsonParser {
public:
    explicit JsonParser(std::string text) : text_(std::move(text)) {}

    JsonValue parse() {
        skip_space();
        JsonValue value = parse_value();
        skip_space();
        if (position_ != text_.size()) fail("Unexpected trailing JSON text");
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(message + " at JSON byte " +
                                 std::to_string(position_));
    }

    void skip_space() {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
    }

    bool consume(const char expected) {
        skip_space();
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    JsonValue parse_value() {
        skip_space();
        if (position_ >= text_.size()) fail("Missing JSON value");
        if (text_[position_] == '{') return parse_object();
        if (text_[position_] == '"') return JsonValue{false, {}, parse_string()};
        if (text_.compare(position_, 4, "true") == 0) {
            position_ += 4;
            return JsonValue{false, {}, true};
        }
        if (text_.compare(position_, 5, "false") == 0) {
            position_ += 5;
            return JsonValue{false, {}, false};
        }
        if (text_.compare(position_, 4, "null") == 0) {
            position_ += 4;
            return JsonValue{false, {}, std::monostate{}};
        }
        return JsonValue{false, {}, parse_number()};
    }

    JsonValue parse_object() {
        if (!consume('{')) fail("Expected JSON object");
        JsonValue object;
        object.object = true;
        if (consume('}')) return object;

        while (true) {
            skip_space();
            if (position_ >= text_.size() || text_[position_] != '"') {
                fail("Expected JSON object key");
            }
            const std::string key = parse_string();
            if (!consume(':')) fail("Expected ':' after JSON object key");
            if (!object.members.emplace(key, parse_value()).second) {
                fail("Duplicate JSON object key '" + key + "'");
            }
            if (consume('}')) return object;
            if (!consume(',')) fail("Expected ',' between JSON object members");
        }
    }

    std::string parse_string() {
        if (position_ >= text_.size() || text_[position_] != '"') {
            fail("Expected JSON string");
        }
        ++position_;
        std::string value;
        while (position_ < text_.size()) {
            const unsigned char c =
                static_cast<unsigned char>(text_[position_++]);
            if (c == '"') return value;
            if (c < 0x20U) fail("Unescaped control character in JSON string");
            if (c != '\\') {
                value.push_back(static_cast<char>(c));
                continue;
            }
            if (position_ >= text_.size()) fail("Incomplete JSON string escape");
            const char escaped = text_[position_++];
            switch (escaped) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'u': {
                    if (position_ + 4 > text_.size()) {
                        fail("Incomplete JSON Unicode escape");
                    }
                    unsigned code = 0;
                    for (unsigned i = 0; i < 4; ++i) {
                        const char digit = text_[position_++];
                        code <<= 4U;
                        if (digit >= '0' && digit <= '9') code += digit - '0';
                        else if (digit >= 'a' && digit <= 'f') code += digit - 'a' + 10;
                        else if (digit >= 'A' && digit <= 'F') code += digit - 'A' + 10;
                        else fail("Invalid JSON Unicode escape");
                    }
                    // The writer emits \u only for one-byte control values.
                    // Reject broader escapes instead of implementing UTF-16
                    // surrogate handling that this metadata format never emits.
                    if (code > 0x7fU) {
                        fail("Non-ASCII JSON Unicode escape is unsupported");
                    }
                    value.push_back(static_cast<char>(code));
                    break;
                }
                default: fail("Unsupported JSON string escape");
            }
        }
        fail("Unterminated JSON string");
    }

    ConfigValue parse_number() {
        const std::size_t begin = position_;
        if (text_[position_] == '-') ++position_;
        if (position_ >= text_.size() ||
            !std::isdigit(static_cast<unsigned char>(text_[position_]))) {
            fail("Invalid JSON number");
        }
        if (text_[position_] == '0') {
            ++position_;
        } else {
            while (position_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
        }
        bool floating = false;
        if (position_ < text_.size() && text_[position_] == '.') {
            floating = true;
            ++position_;
            if (position_ >= text_.size() ||
                !std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                fail("Invalid JSON fractional number");
            }
            while (position_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
        }
        if (position_ < text_.size() &&
            (text_[position_] == 'e' || text_[position_] == 'E')) {
            floating = true;
            ++position_;
            if (position_ < text_.size() &&
                (text_[position_] == '+' || text_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= text_.size() ||
                !std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                fail("Invalid JSON exponent");
            }
            while (position_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
        }

        const std::string token = text_.substr(begin, position_ - begin);
        char* end = nullptr;
        if (!floating) {
            errno = 0;
            const long long value = std::strtoll(token.c_str(), &end, 10);
            if (errno == 0 && end != token.c_str() && *end == '\0') {
                return static_cast<std::int64_t>(value);
            }
        }
        errno = 0;
        const double value = std::strtod(token.c_str(), &end);
        if (errno != 0 || end == token.c_str() || *end != '\0' ||
            !std::isfinite(value)) {
            fail("Invalid or non-finite JSON number");
        }
        return value;
    }

    std::string text_;
    std::size_t position_ = 0;
};

void write_json_string(std::ostream& output, const std::string& value) {
    output << '"';
    for (const unsigned char c : value) {
        switch (c) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (c < 0x20U) {
                    output << "\\u" << std::hex << std::setw(4)
                           << std::setfill('0') << static_cast<unsigned>(c)
                           << std::dec << std::setfill(' ');
                } else {
                    output << static_cast<char>(c);
                }
        }
    }
    output << '"';
}

void write_json_scalar(std::ostream& output, const ConfigValue& value) {
    std::visit([&](const auto& typed) {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, std::monostate>) output << "null";
        else if constexpr (std::is_same_v<T, std::string>) write_json_string(output, typed);
        else if constexpr (std::is_same_v<T, bool>) output << (typed ? "true" : "false");
        else if constexpr (std::is_same_v<T, double>) {
            if (std::isfinite(typed)) output << std::setprecision(15) << typed;
            else output << "null";
        } else output << typed;
    }, value);
}

void write_json_map(std::ostream& output,
                    const std::map<std::string, ConfigValue>& values,
                    const unsigned indent) {
    output << '{';
    if (!values.empty()) output << '\n';
    std::size_t written = 0;
    for (const auto& [key, value] : values) {
        output << std::string(indent + 2, ' ');
        write_json_string(output, key);
        output << ": ";
        write_json_scalar(output, value);
        if (++written < values.size()) output << ',';
        output << '\n';
    }
    if (!values.empty()) output << std::string(indent, ' ');
    output << '}';
}

void write_atomic_text(const std::filesystem::path& path,
                       const std::string& text) {
    const std::filesystem::path temporary = temporary_artifact_path(path);
    try {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Unable to open temporary metadata file '" +
                                     temporary.string() + "'");
        }
        output << text;
        output.close();
        if (!output) {
            throw std::runtime_error("Failed while writing metadata file '" +
                                     temporary.string() + "'");
        }
        publish_artifact(temporary, path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

const JsonValue& member(const JsonValue& object,
                        const std::string& key,
                        const std::string& context) {
    if (!object.object) throw std::runtime_error(context + " must be a JSON object");
    const auto found = object.members.find(key);
    if (found == object.members.end()) {
        throw std::runtime_error("Missing " + context + "." + key);
    }
    return found->second;
}

const std::string& string_value(const JsonValue& value,
                                const std::string& context) {
    if (value.object) throw std::runtime_error(context + " must be a string");
    const auto* text = std::get_if<std::string>(&value.scalar);
    if (!text) throw std::runtime_error(context + " must be a string");
    return *text;
}

std::int64_t integer_value(const JsonValue& value,
                           const std::string& context) {
    if (value.object) throw std::runtime_error(context + " must be an integer");
    const auto* integer = std::get_if<std::int64_t>(&value.scalar);
    if (!integer) throw std::runtime_error(context + " must be an integer");
    return *integer;
}

std::uint64_t nonnegative_u64(const JsonValue& value,
                              const std::string& context) {
    const std::int64_t integer = integer_value(value, context);
    if (integer < 0) throw std::runtime_error(context + " cannot be negative");
    return static_cast<std::uint64_t>(integer);
}

double number_value(const JsonValue& value, const std::string& context) {
    if (value.object) throw std::runtime_error(context + " must be numeric");
    if (const auto* number = std::get_if<double>(&value.scalar)) return *number;
    if (const auto* integer = std::get_if<std::int64_t>(&value.scalar)) {
        return static_cast<double>(*integer);
    }
    throw std::runtime_error(context + " must be numeric");
}

std::map<std::string, ConfigValue> scalar_map(const JsonValue& value,
                                               const std::string& context) {
    if (!value.object) throw std::runtime_error(context + " must be an object");
    std::map<std::string, ConfigValue> result;
    for (const auto& [key, child] : value.members) {
        if (child.object) {
            throw std::runtime_error(context + "." + key + " must be scalar");
        }
        result.emplace(key, child.scalar);
    }
    return result;
}

std::string toml_quote(const std::string& value) {
    std::string quoted = "\"";
    for (const char c : value) {
        switch (c) {
            case '"': quoted += "\\\""; break;
            case '\\': quoted += "\\\\"; break;
            case '\n': quoted += "\\n"; break;
            case '\r': quoted += "\\r"; break;
            case '\t': quoted += "\\t"; break;
            default: quoted += c;
        }
    }
    quoted += '"';
    return quoted;
}

}  // namespace

void write_graph_preparation_report(
    const std::filesystem::path& report_path,
    const std::filesystem::path& artifact_path,
    const GraphPreparationRecord& record) {
    std::ostringstream output;
    output << std::setprecision(15);
    output << "{\n";
    output << "  \"schema_version\": " << kPreparationSchemaVersion << ",\n";
    output << "  \"artifact\": {\n";
    output << "    \"format\": \"cgf\",\n";
    output << "    \"format_version\": " << kCgfFormatVersion << ",\n";
    output << "    \"encoding\": \"delta-vbyte\",\n";
    output << "    \"path\": ";
    write_json_string(output, artifact_path.filename().string());
    output << ",\n    \"file_bytes\": " << record.artifact.file_bytes << ",\n";
    output << "    \"file_checksum_sha256\": ";
    write_json_string(output, record.artifact.file_checksum_sha256);
    output << "\n  },\n";

    output << "  \"graph\": {\n";
    output << "    \"name\": "; write_json_string(output, record.metadata.name);
    output << ",\n    \"source\": "; write_json_string(output, record.metadata.source);
    output << ",\n    \"type\": "; write_json_string(output, record.metadata.type);
    output << ",\n    \"origin_specification_path\": ";
    write_json_string(output, record.metadata.origin_specification_path);
    output << ",\n    \"parameters\": ";
    write_json_map(output, record.metadata.parameters, 4);
    output << ",\n    \"generation_statistics\": ";
    write_json_map(output, record.metadata.generation_statistics, 4);
    output << ",\n    \"n\": " << record.n;
    output << ",\n    \"m\": " << record.m;
    output << ",\n    \"canonical_fingerprint_sha256\": ";
    write_json_string(output, record.artifact.graph_fingerprint_sha256);
    output << "\n  },\n";

    output << "  \"preparation\": {\n";
    output << "    \"graph_time_seconds\": " << record.graph_time_seconds << ",\n";
    output << "    \"encoding_and_write_time_seconds\": "
           << record.encoding_and_write_time_seconds << "\n";
    output << "  }\n";
    output << "}\n";
    write_atomic_text(report_path, output.str());
}

GraphPreparationRecord read_graph_preparation_report(
    const std::filesystem::path& report_path) {
    std::ifstream input(report_path);
    if (!input) {
        throw std::runtime_error("Unable to open graph preparation report '" +
                                 report_path.string() + "'");
    }
    std::ostringstream text;
    text << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error("Failed while reading graph preparation report");
    }

    const JsonValue root = JsonParser(text.str()).parse();
    if (integer_value(member(root, "schema_version", "root"),
                      "schema_version") != kPreparationSchemaVersion) {
        throw std::runtime_error("Unsupported graph preparation report schema");
    }

    GraphPreparationRecord record;
    const JsonValue& artifact = member(root, "artifact", "root");
    if (string_value(member(artifact, "format", "artifact"),
                     "artifact.format") != "cgf") {
        throw std::runtime_error("Preparation report does not describe CGF");
    }
    if (integer_value(member(artifact, "format_version", "artifact"),
                      "artifact.format_version") != kCgfFormatVersion) {
        throw std::runtime_error("Preparation report has an unsupported CGF version");
    }
    if (string_value(member(artifact, "encoding", "artifact"),
                     "artifact.encoding") != "delta-vbyte") {
        throw std::runtime_error("Preparation report has an unsupported CGF encoding");
    }
    record.metadata.prepared_artifact_path =
        (report_path.parent_path() /
         string_value(member(artifact, "path", "artifact"), "artifact.path"))
            .lexically_normal().string();
    record.artifact.file_bytes = nonnegative_u64(
        member(artifact, "file_bytes", "artifact"), "artifact.file_bytes");
    record.artifact.file_checksum_sha256 = string_value(
        member(artifact, "file_checksum_sha256", "artifact"),
        "artifact.file_checksum_sha256");
    parse_sha256_hex(record.artifact.file_checksum_sha256);

    const JsonValue& graph = member(root, "graph", "root");
    record.metadata.name = string_value(member(graph, "name", "graph"), "graph.name");
    record.metadata.source = string_value(member(graph, "source", "graph"), "graph.source");
    record.metadata.type = string_value(member(graph, "type", "graph"), "graph.type");
    record.metadata.origin_specification_path = string_value(
        member(graph, "origin_specification_path", "graph"),
        "graph.origin_specification_path");
    record.metadata.parameters = scalar_map(
        member(graph, "parameters", "graph"), "graph.parameters");
    record.metadata.generation_statistics = scalar_map(
        member(graph, "generation_statistics", "graph"),
        "graph.generation_statistics");
    record.n = nonnegative_u64(member(graph, "n", "graph"), "graph.n");
    record.m = nonnegative_u64(member(graph, "m", "graph"), "graph.m");
    record.artifact.n = record.n;
    record.artifact.m = record.m;
    if (record.m > std::numeric_limits<std::uint64_t>::max() / 2U) {
        throw std::runtime_error("Preparation report m is too large");
    }
    record.artifact.adjacency_entries = 2U * record.m;
    record.artifact.graph_fingerprint_sha256 = string_value(
        member(graph, "canonical_fingerprint_sha256", "graph"),
        "graph.canonical_fingerprint_sha256");
    parse_sha256_hex(record.artifact.graph_fingerprint_sha256);

    const JsonValue& preparation = member(root, "preparation", "root");
    record.graph_time_seconds = number_value(
        member(preparation, "graph_time_seconds", "preparation"),
        "preparation.graph_time_seconds");
    record.encoding_and_write_time_seconds = number_value(
        member(preparation, "encoding_and_write_time_seconds", "preparation"),
        "preparation.encoding_and_write_time_seconds");
    return record;
}

void write_prepared_graph_specification(
    const std::filesystem::path& specification_path,
    const std::string& graph_name,
    const std::filesystem::path& artifact_path,
    const std::filesystem::path& report_path) {
    std::ostringstream output;
    output << "# Generated by cg_prepare. Paths are relative to this file.\n";
    output << "[graph]\n";
    output << "name = " << toml_quote(graph_name) << '\n';
    output << "source = \"file\"\n";
    output << "format = \"cgf\"\n";
    output << "path = " << toml_quote(artifact_path.filename().string()) << '\n';
    output << "preparation_report = "
           << toml_quote(report_path.filename().string()) << '\n';
    output << "preparation_report_sha256 = "
           << toml_quote(sha256_hex(sha256_file_with_zero_range(
                  report_path, 0, 0)))
           << '\n';
    write_atomic_text(specification_path, output.str());
}

}  // namespace cc_con
