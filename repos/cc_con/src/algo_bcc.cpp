#include "cc_con/algo_bcc.hpp"
#include "cc_con/complement_degeneracy.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using namespace std;

namespace cc_con {
namespace {

// The neighborhood of one source vertex is a contiguous range in records.
// Fingerprints collect likely-equal neighborhoods without a hash table; exact
// comparisons below still decide equality.
struct NeighborhoodSignature {
    int source;
    int neighborhood_size;
    size_t neighborhood_begin;
    uint64_t fingerprint_1;
    uint64_t fingerprint_2;
};

// One neighborhood-equivalence class R and its common neighborhood Z. Z stays
// as a range in records until the final CrossCover clique is emitted.
struct CrossCoverGroup {
    vector<int> R;
    size_t neighborhood_begin;
    int neighborhood_size;
};

}  // namespace

void prepare_bcc_cross_cover(
    const vector<vector<int>>& adj,
    long long m,
    vector<vector<int>>& F,
    BccCrossCoverData& cross_data,
    BccStatistics& statistics) {
    int n = (int)adj.size();
    long long possible_edges = 1LL * n * (n - 1) / 2;

    F.clear();
    cross_data = {};
    statistics = {};
    statistics.mu = possible_edges - m + 1;
    if (n == 0) {
        cross_data.part_begin.push_back(0);
        cross_data.pair_row_begin.push_back(0);
        return;
    }

    // BCC Step 1. A degeneracy coloring of the complement partitions V into
    // graph cliques F. Definition 3.21 emits the nontrivial parts obtained by
    // splitting these classes. This implementation instead uses the original F
    // classes as its initial cover; their total assignment is at most n, and
    // they also cover every edge between parts of the same F class. The parts
    // below are therefore needed only for CrossCover calls between F classes.
    statistics.complement_degeneracy =
        complement_degeneracy_coloring(adj, F);
    statistics.num_complement_colors = (long long)F.size();

    // Definition 3.21 sets b = ceil(n/sqrt(mu)) and always splits F classes at
    // that size. We use the following implementation shortcut. The edge-owner
    // argument from the proof of Lemma 3.22 also applies to the family formed
    // here: every clique owns a nonempty edge set, these sets partition E, and
    // each clique contributes at most two assignments per owned edge. Hence
    // sigma(B) <= 2m. When
    //
    //   m < 10^6 * n * sqrt(mu),
    //
    // this part of the bound is already within O(n*sqrt(mu)), so splitting is
    // unnecessary and each F class is used as one temporary part. Otherwise the
    // implementation performs the paper's split.
    // The fixed c below controls only when this shortcut is taken.
    constexpr long double c = 1000000.0L;
    statistics.splitting_applied =
        !((long double)m <
          c * (long double)n *
              sqrt((long double)statistics.mu));

    int b = n;
    if (statistics.splitting_applied) {
        // Compute the smallest integer b satisfying b^2*mu >= n^2. Integer
        // arithmetic prevents a floating-point value near an integer from
        // moving a part boundary.
        long long n_squared = 1LL * n * n;
        long long square_threshold =
            n_squared / statistics.mu +
            (n_squared % statistics.mu == 0 ? 0 : 1);
        long long low = 1;
        long long high = n;
        while (low < high) {
            long long middle = low + (high - low) / 2;
            if (middle * middle >= square_threshold) {
                high = middle;
            } else {
                low = middle + 1;
            }
        }
        b = (int)low;
        statistics.part_size_limit = b;
    }

    // A part is a contiguous chunk of one sorted F class. part_vertices maps a
    // local part position back to a graph vertex and remains valid after F is
    // moved into the output cover. The three local arrays below are needed only
    // while crossing-edge ranks are created and disappear when this function
    // returns.
    vector<int> part_of(n, -1);
    vector<int> position_in_part(n, -1);
    vector<int> part_color;
    cross_data.part_size.reserve(F.size());
    cross_data.part_begin.reserve(F.size() + 1);
    cross_data.part_vertices.reserve(n);
    part_color.reserve(F.size());
    cross_data.part_begin.push_back(0);

    for (int color = 0; color < (int)F.size(); ++color) {
        auto& color_class = F[color];
        int step = statistics.splitting_applied
                       ? b
                       : (int)color_class.size();

        for (int begin = 0; begin < (int)color_class.size(); begin += step) {
            int end = min((int)color_class.size(), begin + step);
            int part = (int)cross_data.part_size.size();
            cross_data.part_size.push_back(end - begin);
            part_color.push_back(color);

            int local_position = 0;
            for (int position = begin; position < end; ++position) {
                int v = color_class[position];
                part_of[v] = part;
                position_in_part[v] = local_position++;
                cross_data.part_vertices.push_back(v);
            }
            cross_data.part_begin.push_back(cross_data.part_vertices.size());
        }
    }
    statistics.num_parts = (long long)cross_data.part_size.size();

    // pair_row_begin[i] is the first rank reserved for a pair whose lower part
    // is i. Inside row i, consecutive blocks represent (i,i+1), (i,i+2), ...;
    // block (i,j) has |P_i|*|P_j| ranks. Thus one integer rank orders records by
    // part pair without storing part ids or consulting part_of during sorting.
    int num_parts = (int)cross_data.part_size.size();
    cross_data.pair_row_begin.assign(num_parts + 1, 0);
    for (int part = 0; part < num_parts; ++part) {
        uint64_t higher_vertices =
            (uint64_t)n - (uint64_t)cross_data.part_begin[part + 1];
        cross_data.pair_row_begin[part + 1] =
            cross_data.pair_row_begin[part] +
            (uint64_t)cross_data.part_size[part] * higher_vertices;
    }

    // Count first so the one-record-per-crossing-edge array has exact capacity.
    // Edges between chunks of the same original F class are omitted: that
    // unsplit class already owns and covers them.
    size_t num_crossing_edges = 0;
    for (int u = 0; u < n; ++u) {
        for (int v : adj[u]) {
            if (u < v &&
                part_color[part_of[u]] != part_color[part_of[v]]) {
                ++num_crossing_edges;
            }
        }
    }
    cross_data.records.reserve(num_crossing_edges);

    for (int u = 0; u < n; ++u) {
        for (int v : adj[u]) {
            if (u >= v ||
                part_color[part_of[u]] == part_color[part_of[v]]) {
                continue;
            }

            int part_i = min(part_of[u], part_of[v]);
            int part_j = max(part_of[u], part_of[v]);
            int endpoint_i = part_of[u] == part_i ? u : v;
            int endpoint_j = part_of[u] == part_j ? u : v;

            // CrossCover(X,Y) groups X by neighborhoods in Y. Choose the smaller
            // part as Y; equal sizes use the lower part as X for determinism.
            bool lower_part_is_target =
                cross_data.part_size[part_i] < cross_data.part_size[part_j];
            int source = lower_part_is_target ? endpoint_j : endpoint_i;
            int target = lower_part_is_target ? endpoint_i : endpoint_j;
            int target_part = lower_part_is_target ? part_i : part_j;

            uint64_t pair_base =
                cross_data.pair_row_begin[part_i] +
                (uint64_t)cross_data.part_size[part_i] *
                    (cross_data.part_begin[part_j] -
                     cross_data.part_begin[part_i + 1]);
            uint64_t rank =
                pair_base +
                (uint64_t)position_in_part[source] *
                    (uint64_t)cross_data.part_size[target_part] +
                (uint64_t)position_in_part[target];
            cross_data.records.push_back(rank);
        }
    }

    // Numeric sorting now performs the complete (part pair, source, target)
    // ordering over contiguous 64-bit values. It requires no comparator and no
    // random reads from endpoint-to-part arrays in the sorting loop.
    sort(cross_data.records.begin(), cross_data.records.end());
}

size_t construct_next_cross_cover_pair(
    const BccCrossCoverData& cross_data,
    size_t record_begin,
    vector<CrossCoverClique>& pair_cover) {
    pair_cover.clear();
    if (record_begin == cross_data.records.size()) return record_begin;

    uint64_t first_rank = cross_data.records[record_begin];

    // Decode the lower part i from its row, then locate the higher part j from
    // the cumulative number of vertices in parts above i. Rank blocks may have
    // gaps because graph edges occupy only some possible endpoint pairs.
    auto row_after = upper_bound(cross_data.pair_row_begin.begin(),
                                 cross_data.pair_row_begin.end(), first_rank);
    int part_i =
        (int)(row_after - cross_data.pair_row_begin.begin() - 1);
    uint64_t within_row = first_rank - cross_data.pair_row_begin[part_i];
    int higher_vertex_offset =
        (int)(within_row / (uint64_t)cross_data.part_size[part_i]);
    size_t flat_vertex_position =
        cross_data.part_begin[part_i + 1] + higher_vertex_offset;
    auto part_after = upper_bound(
        cross_data.part_begin.begin() + part_i + 1,
        cross_data.part_begin.end(), flat_vertex_position);
    int part_j =
        (int)(part_after - cross_data.part_begin.begin() - 1);

    uint64_t pair_base =
        cross_data.pair_row_begin[part_i] +
        (uint64_t)cross_data.part_size[part_i] *
            (cross_data.part_begin[part_j] -
             cross_data.part_begin[part_i + 1]);
    uint64_t pair_limit =
        pair_base +
        (uint64_t)cross_data.part_size[part_i] *
            (uint64_t)cross_data.part_size[part_j];

    size_t record_end = record_begin + 1;
    while (record_end < cross_data.records.size() &&
           cross_data.records[record_end] < pair_limit) {
        ++record_end;
    }

    bool lower_part_is_target =
        cross_data.part_size[part_i] < cross_data.part_size[part_j];
    int source_part =
        lower_part_is_target ? part_j : part_i;
    int target_part =
        lower_part_is_target ? part_i : part_j;
    uint64_t target_size = (uint64_t)cross_data.part_size[target_part];

    // CrossCover for this part pair. Equal neighborhoods among source vertices
    // form the R classes; their common nonempty neighborhood is Z. Each
    // signature refers directly to its source run in records.
    vector<NeighborhoodSignature> signatures;
    size_t signature_begin = record_begin;
    while (signature_begin < record_end) {
        uint64_t first_local_rank =
            cross_data.records[signature_begin] - pair_base;
        int source_local = (int)(first_local_rank / target_size);

        size_t signature_end = signature_begin + 1;
        while (signature_end < record_end &&
               (cross_data.records[signature_end] - pair_base) / target_size ==
                   (uint64_t)source_local) {
            ++signature_end;
        }

        uint64_t fingerprint_1 = 1469598103934665603ULL;
        uint64_t fingerprint_2 = 0x9e3779b97f4a7c15ULL;
        for (size_t position = signature_begin;
             position < signature_end;
             ++position) {
            uint64_t value =
                (cross_data.records[position] - pair_base) % target_size + 1ULL;
            fingerprint_1 ^= value;
            fingerprint_1 *= 1099511628211ULL;
            fingerprint_2 ^=
                value + 0x9e3779b97f4a7c15ULL +
                (fingerprint_2 << 6U) + (fingerprint_2 >> 2U);
        }

        signatures.push_back(
            {cross_data.part_vertices[
                 cross_data.part_begin[source_part] + source_local],
             (int)(signature_end - signature_begin),
             signature_begin,
             fingerprint_1,
             fingerprint_2});
        signature_begin = signature_end;
    }

    sort(signatures.begin(), signatures.end(),
         [](const NeighborhoodSignature& lhs,
            const NeighborhoodSignature& rhs) {
             if (lhs.neighborhood_size != rhs.neighborhood_size) {
                 return lhs.neighborhood_size < rhs.neighborhood_size;
             }
             if (lhs.fingerprint_1 != rhs.fingerprint_1) {
                 return lhs.fingerprint_1 < rhs.fingerprint_1;
             }
             if (lhs.fingerprint_2 != rhs.fingerprint_2) {
                 return lhs.fingerprint_2 < rhs.fingerprint_2;
             }
             return lhs.source < rhs.source;
         });

    auto same_neighborhood =
        [&cross_data, pair_base, target_size](
            const NeighborhoodSignature& lhs,
            const NeighborhoodSignature& rhs) {
            if (lhs.neighborhood_size != rhs.neighborhood_size) return false;
            for (int offset = 0; offset < lhs.neighborhood_size; ++offset) {
                uint64_t left =
                    (cross_data.records[lhs.neighborhood_begin + offset] -
                     pair_base) % target_size;
                uint64_t right =
                    (cross_data.records[rhs.neighborhood_begin + offset] -
                     pair_base) % target_size;
                if (left != right) return false;
            }
            return true;
        };

    vector<CrossCoverGroup> groups;
    int fingerprint_begin = 0;
    while (fingerprint_begin < (int)signatures.size()) {
        int fingerprint_end = fingerprint_begin + 1;
        while (fingerprint_end < (int)signatures.size() &&
               signatures[fingerprint_end].neighborhood_size ==
                   signatures[fingerprint_begin].neighborhood_size &&
               signatures[fingerprint_end].fingerprint_1 ==
                   signatures[fingerprint_begin].fingerprint_1 &&
               signatures[fingerprint_end].fingerprint_2 ==
                   signatures[fingerprint_begin].fingerprint_2) {
            ++fingerprint_end;
        }

        // Fingerprints never decide equality. The usual block is checked by one
        // exact pass; only an actual collision requires exact lexicographic
        // sorting within that block.
        bool single_exact_group = true;
        for (int position = fingerprint_begin + 1;
             position < fingerprint_end;
             ++position) {
            if (!same_neighborhood(signatures[fingerprint_begin],
                                   signatures[position])) {
                single_exact_group = false;
                break;
            }
        }

        if (!single_exact_group) {
            sort(
                signatures.begin() + fingerprint_begin,
                signatures.begin() + fingerprint_end,
                [&cross_data, pair_base, target_size](
                    const NeighborhoodSignature& lhs,
                    const NeighborhoodSignature& rhs) {
                    int common = min(lhs.neighborhood_size,
                                     rhs.neighborhood_size);
                    for (int offset = 0; offset < common; ++offset) {
                        uint64_t left =
                            (cross_data.records[
                                 lhs.neighborhood_begin + offset] - pair_base) %
                            target_size;
                        uint64_t right =
                            (cross_data.records[
                                 rhs.neighborhood_begin + offset] - pair_base) %
                            target_size;
                        if (left != right) return left < right;
                    }
                    if (lhs.neighborhood_size != rhs.neighborhood_size) {
                        return lhs.neighborhood_size < rhs.neighborhood_size;
                    }
                    return lhs.source < rhs.source;
                });
        }

        int exact_begin = fingerprint_begin;
        while (exact_begin < fingerprint_end) {
            int exact_end = exact_begin + 1;
            while (exact_end < fingerprint_end &&
                   same_neighborhood(signatures[exact_begin],
                                     signatures[exact_end])) {
                ++exact_end;
            }

            CrossCoverGroup group;
            group.neighborhood_begin =
                signatures[exact_begin].neighborhood_begin;
            group.neighborhood_size =
                signatures[exact_begin].neighborhood_size;
            group.R.reserve(exact_end - exact_begin);
            for (int position = exact_begin;
                 position < exact_end;
                 ++position) {
                group.R.push_back(signatures[position].source);
            }
            sort(group.R.begin(), group.R.end());
            groups.push_back(std::move(group));
            exact_begin = exact_end;
        }
        fingerprint_begin = fingerprint_end;
    }

    // Fingerprint order is only an implementation detail. Emit classes by their
    // smallest source vertex, matching deterministic part-pair/source order.
    sort(groups.begin(), groups.end(),
         [](const CrossCoverGroup& lhs, const CrossCoverGroup& rhs) {
             return lhs.R.front() < rhs.R.front();
         });

    pair_cover.reserve(groups.size());
    for (auto& group : groups) {
        pair_cover.emplace_back();
        auto& cross_clique = pair_cover.back();
        cross_clique.R = std::move(group.R);
        cross_clique.Z.reserve(group.neighborhood_size);
        for (int offset = 0; offset < group.neighborhood_size; ++offset) {
            int target_local = (int)(
                (cross_data.records[group.neighborhood_begin + offset] -
                 pair_base) %
                target_size);
            cross_clique.Z.push_back(
                cross_data.part_vertices[
                    cross_data.part_begin[target_part] + target_local]);
        }
    }

    return record_end;
}

void construct_bcc(const vector<vector<int>>& adj,
                   long long m,
                   vector<vector<int>>& C,
                   BccStatistics& statistics) {
    vector<vector<int>> F;
    BccCrossCoverData cross_data;
    prepare_bcc_cross_cover(adj, m, F, cross_data, statistics);

    C.clear();
    int num_initial_cliques = 0;
    for (auto& color_class : F) {
        if (color_class.size() >= 2) ++num_initial_cliques;
    }
    C.reserve(num_initial_cliques);

    // The original, unsplit complement-color classes come first. Singleton
    // classes cover no edge, although their vertices remain in the temporary
    // parts used to decode CrossCover records.
    for (auto& color_class : F) {
        if (color_class.size() >= 2) C.push_back(std::move(color_class));
    }
    vector<vector<int>>().swap(F);

    // Construct and merge one nonempty part pair at a time. The final cover C
    // is necessarily retained, but R/Z sets from already emitted pairs are
    // released instead of remaining alongside it.
    vector<CrossCoverClique> pair_cover;
    size_t record_position = 0;
    while (record_position < cross_data.records.size()) {
        record_position = construct_next_cross_cover_pair(
            cross_data, record_position, pair_cover);

        for (auto& cross_clique : pair_cover) {
            C.emplace_back(
                cross_clique.R.size() + cross_clique.Z.size());
            merge(cross_clique.R.begin(), cross_clique.R.end(),
                  cross_clique.Z.begin(), cross_clique.Z.end(),
                  C.back().begin());
        }
    }
}

}  // namespace cc_con
