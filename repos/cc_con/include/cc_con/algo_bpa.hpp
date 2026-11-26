#ifndef CC_CON_ALGO_BPA_HPP
#define CC_CON_ALGO_BPA_HPP

#include "cc_con/config.hpp"

#include <vector>

namespace cc_con {

// The rule used when more than one current-phase label is in A[u] intersect A[v].
// Parsing converts the configuration string once, before construction is timed.
enum class BpaLabelRule {
    MinLabel,
    MaxLabel,
    MinClique,
    MaxClique,
};

struct BpaOptions {
    double tau = 4.0;
    BpaLabelRule label_rule = BpaLabelRule::MinLabel;
};

// The report already supplies |C| and sigma(C). BPA additionally records the
// number of graph-edge passes and peak sigma(D), the quantity that controls
// phase closure.
struct BpaStatistics {
    long long num_phases = 0;
    long long peak_size_D = 0;
};

BpaOptions parse_bpa_options(const Config& config);
const char* bpa_label_rule_name(BpaLabelRule rule);

// adj is the canonical graph: simple, undirected, without isolated vertices, and
// represented by sorted, duplicate-free open adjacency lists. m is computed
// during graph preparation so its O(n) calculation is outside the timed call.
// The function writes the constructed clique cover directly into C.
void construct_bpa(const std::vector<std::vector<int>>& adj,
                   long long m,
                   const BpaOptions& options,
                   std::vector<std::vector<int>>& C,
                   BpaStatistics& statistics);

}  // namespace cc_con

#endif
