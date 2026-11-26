#ifndef CC_CON_CONFIG_HPP
#define CC_CON_CONFIG_HPP

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace cc_con {

// Values supported by cc_con's deliberately small TOML subset.
// std::monostate is used only by report metadata to render an unavailable
// measurement as JSON null. The configuration parser never produces it.
using ConfigValue =
    std::variant<std::monostate, std::int64_t, double, bool, std::string>;

// Strict, read-only configuration with typed access and unknown-key detection.
//
// load() accepts section headers and scalar key/value assignments. Accessors mark
// keys as consumed, allowing main() to reject misspelled or unsupported options
// after all selected modules have parsed their own settings.
class Config {
public:
    static Config load(const std::filesystem::path& path);

    bool has(const std::string& key) const;
    std::string require_string(const std::string& key) const;
    std::string string_or(const std::string& key, const std::string& fallback) const;
    std::int64_t require_integer(const std::string& key) const;
    std::int64_t integer_or(const std::string& key, std::int64_t fallback) const;
    double require_number(const std::string& key) const;
    double number_or(const std::string& key, double fallback) const;
    bool bool_or(const std::string& key, bool fallback) const;

    // Resolves relative paths against the directory containing the config file.
    std::filesystem::path resolve_path(const std::string& value) const;
    // Keys not consumed through one of the typed accessors.
    std::vector<std::string> unused_keys() const;
    const std::filesystem::path& source_path() const { return source_path_; }

private:
    const ConfigValue& consume(const std::string& key) const;

    std::filesystem::path source_path_;
    std::map<std::string, ConfigValue> values_;
    mutable std::set<std::string> consumed_;
};

struct CommonOptions {
    // Pipeline settings shared by all constructions. They are deliberately not
    // part of an algorithm's option type or timed construction call.
    bool assignment_minimal = false;
    bool verification = false;
    bool write_cover = false;
    std::filesystem::path cover_directory;
};

CommonOptions parse_common_options(const Config& config);

}  // namespace cc_con

#endif
