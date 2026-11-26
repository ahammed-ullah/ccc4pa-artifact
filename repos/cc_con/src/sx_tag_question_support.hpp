#ifndef CC_CON_SX_TAG_QUESTION_SUPPORT_HPP
#define CC_CON_SX_TAG_QUESTION_SUPPORT_HPP

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace cc_con::sx_tag_question_detail {

// Parsed native incidence data for one Stack Exchange community. The vectors
// contain dense temporary question IDs; external IDs remain local to parsing
// and never reach the graph or a clique-cover construction.
struct SxTagQuestionData {
    std::vector<std::vector<int>> questions_by_tag;
    std::vector<std::int64_t> external_tag_ids;

    std::int64_t question_count = 0;
    std::int64_t tag_count = 0;
    std::int64_t nontrivial_tag_count = 0;
    std::int64_t incidence_sum = 0;
    std::int64_t incidence_sum_nontrivial = 0;
    std::int64_t min_tag_degree = 0;
    std::int64_t max_tag_degree = 0;
    std::int64_t pair_occurrences = 0;

    // Each pair is (external tag ID, distinct question degree), ordered by
    // decreasing degree and then increasing tag ID.
    std::vector<std::pair<std::int64_t, std::int64_t>> largest_tag_degrees;
};

// Reads one extracted *.txt community file, removes repeated (tag, question)
// incidences, and computes every statistic that is available before projection.
SxTagQuestionData read_sx_tag_question_file(
    const std::filesystem::path& path);

double mean_tag_degree(const SxTagQuestionData& data);
double potential_cr(const SxTagQuestionData& data);

// The graph reader calls this before allocating projected edges so a large
// community's native incidence and pair-occurrence counts are visible first.
void print_prescreen_statistics(const std::string& graph_name,
                                const SxTagQuestionData& data,
                                std::ostream& output);

}  // namespace cc_con::sx_tag_question_detail

#endif
