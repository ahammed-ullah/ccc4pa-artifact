#include "sx_tag_question_support.hpp"

#include "cornell_projection_support.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cc_con::sx_tag_question_detail {
namespace {

bool parse_assignment_record(const std::string& line,
                             const std::int64_t line_number,
                             const std::string& source_name,
                             std::int64_t& tag_id,
                             std::int64_t& question_id) {
    const char* position = line.data();
    const char* const end = line.data() + line.size();

    const auto skip_space = [&]() {
        while (position != end &&
               std::isspace(static_cast<unsigned char>(*position))) {
            ++position;
        }
    };
    const auto malformed = [&]() {
        throw std::runtime_error(
            "Malformed Stack Exchange tag-question record in '" +
            source_name + "' at line " + std::to_string(line_number));
    };

    skip_space();
    if (position == end) return false;

    // Column 1 is user_id. It has no role in the projection, so skip the token
    // without converting or retaining it.
    while (position != end &&
           !std::isspace(static_cast<unsigned char>(*position))) {
        ++position;
    }
    if (position == end) malformed();
    skip_space();

    // Columns 2 and 3 are the only semantic input to this reader. from_chars
    // avoids allocating temporary strings for every incidence in a large file.
    const auto parsed_tag = std::from_chars(position, end, tag_id);
    if (parsed_tag.ec != std::errc() || parsed_tag.ptr == position ||
        (parsed_tag.ptr != end &&
         !std::isspace(static_cast<unsigned char>(*parsed_tag.ptr)))) {
        malformed();
    }
    position = parsed_tag.ptr;
    if (position == end) malformed();
    skip_space();

    const auto parsed_question = std::from_chars(position, end, question_id);
    if (parsed_question.ec != std::errc() ||
        parsed_question.ptr == position ||
        (parsed_question.ptr != end &&
         !std::isspace(static_cast<unsigned char>(*parsed_question.ptr)))) {
        malformed();
    }
    position = parsed_question.ptr;
    if (position == end) malformed();
    skip_space();

    // Column 4 is the timestamp. Require one token so truncated records are
    // rejected, but do not parse or store a value that the projection ignores.
    if (position == end) malformed();
    while (position != end &&
           !std::isspace(static_cast<unsigned char>(*position))) {
        ++position;
    }
    skip_space();
    if (position != end) malformed();
    return true;
}

}  // namespace

SxTagQuestionData read_sx_tag_question_file(
    const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(
            "Unable to open Stack Exchange tag-question file '" +
            path.string() + "'");
    }

    SxTagQuestionData data;

    // Only these two maps know the arbitrary external IDs. Lists stored in
    // questions_by_tag use dense int question IDs, which keeps the incidence
    // storage at one int per parsed assignment before duplicate removal.
    std::unordered_map<std::int64_t, int> question_to_dense;
    std::unordered_map<std::int64_t, std::size_t> tag_to_dense;

    std::string line;
    std::int64_t line_number = 0;
    while (std::getline(input, line)) {
        cornell_detail::add_to_counter(
            line_number, 1, "Stack Exchange input line count");

        std::int64_t external_tag = 0;
        std::int64_t external_question = 0;
        if (!parse_assignment_record(
                line, line_number, path.string(),
                external_tag, external_question)) {
            continue;
        }

        auto question = question_to_dense.find(external_question);
        if (question == question_to_dense.end()) {
            if (question_to_dense.size() >=
                static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                throw std::runtime_error(
                    "Stack Exchange community has too many questions for int "
                    "graph identifiers");
            }
            const int dense_question =
                static_cast<int>(question_to_dense.size());
            question = question_to_dense.emplace(
                external_question, dense_question).first;
        }

        auto tag = tag_to_dense.find(external_tag);
        if (tag == tag_to_dense.end()) {
            const std::size_t dense_tag = data.questions_by_tag.size();
            tag = tag_to_dense.emplace(external_tag, dense_tag).first;
            data.questions_by_tag.emplace_back();
            data.external_tag_ids.push_back(external_tag);
        }
        data.questions_by_tag[tag->second].push_back(question->second);
    }
    if (input.bad()) {
        throw std::runtime_error(
            "Failed while reading Stack Exchange tag-question file '" +
            path.string() + "'");
    }

    if (question_to_dense.size() > static_cast<std::size_t>(
                                       std::numeric_limits<std::int64_t>::max()) ||
        data.questions_by_tag.size() > static_cast<std::size_t>(
                                           std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error(
            "Stack Exchange source counts exceed signed 64-bit range");
    }
    data.question_count =
        static_cast<std::int64_t>(question_to_dense.size());
    data.tag_count =
        static_cast<std::int64_t>(data.questions_by_tag.size());

    std::vector<std::pair<std::int64_t, std::int64_t>> tag_degrees;
    tag_degrees.reserve(data.questions_by_tag.size());
    data.min_tag_degree = data.tag_count == 0
        ? 0
        : std::numeric_limits<std::int64_t>::max();

    for (std::size_t tag = 0; tag < data.questions_by_tag.size(); ++tag) {
        std::vector<int>& questions = data.questions_by_tag[tag];

        // Repeated source records for the same (tag, question) incidence must
        // not inflate degrees, P, or the native cover size. Sorting each tag
        // list uses no graph-sized duplicate hash and leaves projection members
        // in the order required for straightforward unordered-pair generation.
        std::sort(questions.begin(), questions.end());
        questions.erase(
            std::unique(questions.begin(), questions.end()), questions.end());

        const std::int64_t degree =
            static_cast<std::int64_t>(questions.size());
        cornell_detail::add_to_counter(
            data.incidence_sum,
            static_cast<std::uint64_t>(questions.size()),
            "Stack Exchange incidence sum");
        data.min_tag_degree = std::min(data.min_tag_degree, degree);
        data.max_tag_degree = std::max(data.max_tag_degree, degree);

        if (questions.size() >= 2) {
            cornell_detail::add_to_counter(
                data.nontrivial_tag_count, 1,
                "Stack Exchange nontrivial tag count");
            cornell_detail::add_to_counter(
                data.incidence_sum_nontrivial,
                static_cast<std::uint64_t>(questions.size()),
                "Stack Exchange nontrivial incidence sum");
        }
        cornell_detail::add_to_counter(
            data.pair_occurrences,
            cornell_detail::choose_two(questions.size()),
            "Stack Exchange pair-occurrence count");
        tag_degrees.emplace_back(data.external_tag_ids[tag], degree);
    }

    std::sort(tag_degrees.begin(), tag_degrees.end(),
              [](const auto& left, const auto& right) {
                  if (left.second != right.second) {
                      return left.second > right.second;
                  }
                  return left.first < right.first;
              });
    if (tag_degrees.size() > 10) tag_degrees.resize(10);
    data.largest_tag_degrees = std::move(tag_degrees);
    return data;
}

double mean_tag_degree(const SxTagQuestionData& data) {
    return static_cast<double>(data.incidence_sum) /
           static_cast<double>(data.tag_count);
}

double potential_cr(const SxTagQuestionData& data) {
    return 2.0 * static_cast<double>(data.pair_occurrences) /
           static_cast<double>(data.incidence_sum_nontrivial);
}

void print_prescreen_statistics(const std::string& graph_name,
                                const SxTagQuestionData& data,
                                std::ostream& output) {
    output << "cc_con: " << graph_name
           << ": Stack Exchange tag-question prescreen"
           << ": question_count=" << data.question_count
           << ", tag_count=" << data.tag_count
           << ", nontrivial_tag_count=" << data.nontrivial_tag_count
           << ", incidence_sum=" << data.incidence_sum
           << ", incidence_sum_nontrivial="
           << data.incidence_sum_nontrivial
           << ", min_tag_degree=";
    if (data.tag_count == 0) {
        output << "NA";
    } else {
        output << data.min_tag_degree;
    }
    output << ", max_tag_degree=";
    if (data.tag_count == 0) {
        output << "NA";
    } else {
        output << data.max_tag_degree;
    }
    output << ", mean_tag_degree=";
    if (data.tag_count == 0) {
        output << "NA";
    } else {
        output << mean_tag_degree(data);
    }
    output << ", pair_occurrences=" << data.pair_occurrences
           << ", potential_CR=";
    if (data.incidence_sum_nontrivial == 0) {
        output << "NA";
    } else {
        output << potential_cr(data);
    }
    output << '\n';

    output << "cc_con: " << graph_name << ": largest_tag_degrees=";
    if (data.largest_tag_degrees.empty()) {
        output << "NA";
    } else {
        for (std::size_t i = 0; i < data.largest_tag_degrees.size(); ++i) {
            if (i != 0) output << ',';
            output << data.largest_tag_degrees[i].first << ':'
                   << data.largest_tag_degrees[i].second;
        }
    }
    output << '\n';
}

}  // namespace cc_con::sx_tag_question_detail
