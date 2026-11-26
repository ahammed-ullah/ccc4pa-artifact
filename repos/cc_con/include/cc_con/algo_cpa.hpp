#ifndef CC_CON_ALGO_CPA_HPP
#define CC_CON_ALGO_CPA_HPP

#include "cc_con/config.hpp"

#include <vector>

namespace cc_con {

// Figure 4 permits any label in its intersection L. These four deterministic
// rules are the same choices offered by BPA and FPA.
enum class CpaLabelRule {
    MinLabel,
    MaxLabel,
    MinClique,
    MaxClique,
};

struct CpaOptions {
    double tau = 4.0;
    CpaLabelRule label_rule = CpaLabelRule::MinLabel;
};

struct CpaStatistics {
    long long mu = 0;
    long long part_size_limit = 0;
    bool splitting_applied = false;
    long long complement_degeneracy = 0;
    long long num_complement_colors = 0;
    long long num_parts = 0;
    long long bcc_num_cliques = 0;
    long long bcc_cover_size = 0;
    long long num_phases = 0;
    long long peak_size_D = 0;
};

CpaOptions parse_cpa_options(const Config& config);
const char* cpa_label_rule_name(CpaLabelRule rule);

// CrossCover-PA (CPA). The BCC components are constructed inside this timed
// function. The complement-degeneracy classes initialize the cover directly;
// phased admissibility processes only the CrossCover cliques.
void construct_cpa(const std::vector<std::vector<int>>& adj,
                   long long m,
                   const CpaOptions& options,
                   std::vector<std::vector<int>>& C,
                   CpaStatistics& statistics);

}  // namespace cc_con

#endif
