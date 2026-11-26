#ifndef CC_CON_ALGO_FPA_HPP
#define CC_CON_ALGO_FPA_HPP

#include "cc_con/config.hpp"

#include <vector>

namespace cc_con {

// The rule used when more than one current-phase label is in A[u] intersect A[v].
// FPA has its own enum so its implementation and configuration remain readable
// without depending on BPA's algorithm-specific types.
enum class FpaLabelRule {
    MinLabel,
    MaxLabel,
    MinClique,
    MaxClique,
};

struct FpaOptions {
    double tau = 4.0;
    FpaLabelRule label_rule = FpaLabelRule::MinLabel;
};

// The report already supplies |C| and sigma(C). FPA additionally records the
// number of nonempty pair passes and peak sigma(D), the quantity that controls
// phase closure.
struct FpaStatistics {
    long long num_phases = 0;
    long long peak_size_D = 0;
};

FpaOptions parse_fpa_options(const Config& config);
const char* fpa_label_rule_name(FpaLabelRule rule);

// First-fit phased-admissibility construction. adj is the canonical graph:
// simple, undirected, without isolated vertices, and represented by sorted,
// duplicate-free open adjacency lists. m is calculated before this timed call.
// The function writes the constructed clique cover directly into C.
void construct_fpa(const std::vector<std::vector<int>>& adj,
                   long long m,
                   const FpaOptions& options,
                   std::vector<std::vector<int>>& C,
                   FpaStatistics& statistics);

}  // namespace cc_con

#endif
