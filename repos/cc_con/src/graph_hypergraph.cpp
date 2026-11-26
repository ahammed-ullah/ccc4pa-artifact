#include "cc_con/graph.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cc_con {
namespace {

enum class HypergraphDistribution {
    KUniform,
    Poisson,
    Lognormal,
    PowerLaw,
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

void reject_unused_hypergraph_keys(const Config& config,
                                   const std::string& distribution) {
    // This check occurs after the selected distribution has consumed exactly its
    // parameters, but before an RNG or graph-sized data structure is created.
    // A parameter belonging to another distribution is therefore reported as an
    // input error instead of being silently ignored during a long generation.
    const std::vector<std::string> unused = config.unused_keys();
    if (unused.empty()) return;

    std::string message = "Unknown or inapplicable graph configuration key";
    if (unused.size() > 1) message += 's';
    message += " for hypergraph distribution '" + distribution + "':";
    for (const std::string& key : unused) message += " " + key;
    throw std::runtime_error(message);
}

std::uint64_t mix_edge_key(std::uint64_t value) {
    // A fixed 64-bit finalizer spreads nearby encoded endpoint pairs across a
    // power-of-two table. The hash is only an internal duplicate index; it has no
    // effect on the generated graph once an edge has been accepted.
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return value;
}

class FlatEdgeSet {
public:
    void reserve_for_edges(const std::uint64_t num_edges) {
        if (num_edges == 0) return;

        // Choose the final power-of-two table before projection starts. This
        // avoids the old-table-plus-new-table peak that made a late rehash much
        // larger than the graph itself.
        std::size_t capacity = 16;
        while (num_edges > static_cast<std::uint64_t>(capacity * 7 / 10)) {
            if (capacity > slots_.max_size() / 2) {
                throw std::runtime_error(
                    "Hypergraph edge hash exceeds addressable memory");
            }
            capacity *= 2;
        }
        slots_.assign(capacity, 0);
    }

    bool insert(const std::uint64_t key) {
        if (slots_.empty()) slots_.assign(16, 0);

        std::size_t position = find_position(key);
        if (slots_[position] == key) return false;

        // Search before growing so a duplicate encountered at the load limit
        // does not trigger an unnecessary rehash.
        if (size_ >= slots_.size() * 7 / 10) {
            grow();
            position = find_position(key);
        }

        slots_[position] = key;
        ++size_;
        return true;
    }

private:
    std::size_t find_position(const std::uint64_t key) const {
        const std::size_t mask = slots_.size() - 1;
        std::size_t position = static_cast<std::size_t>(mix_edge_key(key)) & mask;
        while (slots_[position] != 0 && slots_[position] != key) {
            position = (position + 1) & mask;
        }
        return position;
    }

    void grow() {
        if (slots_.size() > slots_.max_size() / 2) {
            throw std::runtime_error("Hypergraph edge hash exceeds addressable memory");
        }

        std::vector<std::uint64_t> old_slots = std::move(slots_);
        slots_.assign(old_slots.size() * 2, 0);
        for (const std::uint64_t key : old_slots) {
            if (key != 0) slots_[find_position(key)] = key;
        }
    }

    // Zero is the empty marker. A valid canonical edge has u < v, so the packed
    // key (u << 32) | v is always nonzero, including when u is vertex zero.
    std::vector<std::uint64_t> slots_;
    std::size_t size_ = 0;
};

std::uint64_t edge_key(const int first, const int second) {
    const int u = std::min(first, second);
    const int v = std::max(first, second);
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(u)) << 32) |
           static_cast<std::uint32_t>(v);
}

int sample_hyperedge_size(
    const HypergraphDistribution distribution,
    const int n,
    const int k,
    const int s_min,
    const int s_max,
    const double largest_accepted_log_value,
    std::mt19937& generator,
    std::poisson_distribution<long long>& poisson,
    std::normal_distribution<double>& normal,
    std::uniform_real_distribution<long double>& unit_interval,
    const std::vector<long double>& power_law_cdf) {
    constexpr int max_size_sampling_attempts = 1000;

    if (distribution == HypergraphDistribution::KUniform) return k;

    if (distribution == HypergraphDistribution::Poisson) {
        for (int attempt = 0; attempt < max_size_sampling_attempts; ++attempt) {
            const long long sampled = poisson(generator);
            if (sampled <= n) return static_cast<int>(sampled);
        }
        throw std::runtime_error(
            "Poisson hyperedge-size sampling exceeded 1000 attempts");
    }

    if (distribution == HypergraphDistribution::Lognormal) {
        for (int attempt = 0; attempt < max_size_sampling_attempts; ++attempt) {
            const double normal_sample = normal(generator);
            if (!std::isfinite(normal_sample)) {
                if (normal_sample < 0.0) return 0;
                continue;
            }
            if (normal_sample > largest_accepted_log_value) continue;

            const long long sampled = std::llround(std::exp(normal_sample));
            if (sampled <= n) return static_cast<int>(sampled);
        }
        throw std::runtime_error(
            "Lognormal hyperedge-size sampling exceeded 1000 attempts");
    }

    const long double draw = unit_interval(generator);
    const auto position =
        std::lower_bound(power_law_cdf.begin(), power_law_cdf.end(), draw);
    if (position == power_law_cdf.end()) return s_max;
    const std::size_t offset = static_cast<std::size_t>(
        std::distance(power_law_cdf.begin(), position));
    return s_min + static_cast<int>(offset);
}

}  // namespace

void generate_hypergraph_graph(const Config& config,
                               const std::string& configured_name,
                               std::vector<std::vector<int>>& adj,
                               GraphMetadata& metadata) {
    const std::string distribution_name =
        lower(config.require_string("graph.parameters.distribution"));
    const std::int64_t requested_n = config.require_integer("graph.parameters.n");
    const std::int64_t requested_r = config.require_integer("graph.parameters.r");
    const std::int64_t seed_value = config.integer_or("graph.parameters.seed", 19);

    if (requested_n < 2 || requested_n > std::numeric_limits<int>::max()) {
        throw std::runtime_error("Hypergraph parameter n must be in [2, INT_MAX]");
    }
    if (requested_r < 0) {
        throw std::runtime_error("Hypergraph parameter r must be nonnegative");
    }
    if (seed_value < 0 ||
        static_cast<std::uint64_t>(seed_value) >
            std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(
            "Hypergraph seed must fit an unsigned 32-bit integer");
    }

    const int n = static_cast<int>(requested_n);
    HypergraphDistribution distribution;
    int k = 2;
    double lambda = 1.0;
    double mu = 0.0;
    double sigma = 1.0;
    double alpha = 1.0;
    int s_min = 2;
    int s_max = 2;

    // Only the selected branch consumes distribution-specific keys. The unused
    // key check below consequently catches combinations such as k with Poisson.
    if (distribution_name == "k-uniform") {
        distribution = HypergraphDistribution::KUniform;
        const std::int64_t value = config.require_integer("graph.parameters.k");
        if (value < 2 || value > requested_n) {
            throw std::runtime_error("K-uniform parameter k must be in [2,n]");
        }
        k = static_cast<int>(value);
    } else if (distribution_name == "poisson") {
        distribution = HypergraphDistribution::Poisson;
        lambda = config.require_number("graph.parameters.lambda");
        if (!std::isfinite(lambda) || lambda <= 0.0 ||
            lambda > static_cast<double>(n)) {
            throw std::runtime_error(
                "Poisson parameter lambda must be finite and in (0,n]");
        }
    } else if (distribution_name == "lognormal") {
        distribution = HypergraphDistribution::Lognormal;
        mu = config.require_number("graph.parameters.mu");
        sigma = config.require_number("graph.parameters.sigma");
        if (!std::isfinite(mu)) {
            throw std::runtime_error("Lognormal parameter mu must be finite");
        }
        if (!std::isfinite(sigma) || sigma <= 0.0) {
            throw std::runtime_error(
                "Lognormal parameter sigma must be finite and positive");
        }
        if (mu > std::log(static_cast<double>(n) + 0.5)) {
            throw std::runtime_error(
                "Lognormal parameter mu must be at most log(n + 0.5)");
        }
    } else if (distribution_name == "power-law") {
        distribution = HypergraphDistribution::PowerLaw;
        alpha = config.require_number("graph.parameters.alpha");
        const std::int64_t minimum =
            config.require_integer("graph.parameters.s_min");
        const std::int64_t maximum =
            config.require_integer("graph.parameters.s_max");
        if (!std::isfinite(alpha) || alpha <= 0.0) {
            throw std::runtime_error(
                "Power-law parameter alpha must be finite and positive");
        }
        if (minimum < 2 || minimum > maximum || maximum > requested_n) {
            throw std::runtime_error(
                "Power-law parameters must satisfy 2 <= s_min <= s_max <= n");
        }
        s_min = static_cast<int>(minimum);
        s_max = static_cast<int>(maximum);
    } else {
        throw std::runtime_error(
            "Hypergraph distribution must be 'k-uniform', 'poisson', "
            "'lognormal', or 'power-law'");
    }

    reject_unused_hypergraph_keys(config, distribution_name);

    // The discrete power-law CDF is constructed once. Scaling every weight by
    // s_min^alpha leaves the distribution unchanged and gives the first weight
    // value one, preventing all weights from underflowing for a large alpha.
    std::vector<long double> power_law_cdf;
    if (distribution == HypergraphDistribution::PowerLaw) {
        const std::size_t number_of_sizes =
            static_cast<std::size_t>(s_max - s_min) + 1;
        power_law_cdf.reserve(number_of_sizes);

        long double total_weight = 0.0L;
        for (std::size_t offset = 0; offset < number_of_sizes; ++offset) {
            const long double size =
                static_cast<long double>(s_min) + static_cast<long double>(offset);
            const long double ratio = static_cast<long double>(s_min) / size;
            total_weight += std::pow(ratio, static_cast<long double>(alpha));
            power_law_cdf.push_back(total_weight);
        }
        for (long double& cumulative : power_law_cdf) cumulative /= total_weight;
        power_law_cdf.back() = 1.0L;
    }

    const std::uint32_t seed = static_cast<std::uint32_t>(seed_value);
    const double largest_accepted_log_value =
        std::log(static_cast<double>(n) + 0.5);

    std::int64_t nontrivial_hyperedges = 0;
    std::int64_t sampled_size_sum = 0;
    int sampled_size_min = 0;
    int sampled_size_max = 0;
    std::int64_t pair_occurrences = 0;
    long double log_pair_absence = 0.0L;
    const long double ordered_vertex_pairs =
        static_cast<long double>(n) * static_cast<long double>(n - 1);

    // Pass one samples the exact hyperedge sequence without projecting its
    // quadratic pair sets. For each selected vertex, one size-s hyperedge will
    // append s-1 adjacency entries during replay, so these counts provide exact
    // reserve sizes for the low-duplication path.
    std::vector<std::uint64_t> adjacency_occurrences(
        static_cast<std::size_t>(n), 0);
    {
        std::mt19937 generator(seed);
        // Use a wide result type because a valid Poisson distribution still has a
        // tail above INT_MAX when lambda is near the largest supported n.
        std::poisson_distribution<long long> poisson(lambda);
        std::normal_distribution<double> normal(mu, sigma);
        std::uniform_real_distribution<long double> unit_interval(0.0L, 1.0L);
        std::uniform_int_distribution<int> random_position;
        std::vector<int> permutation(static_cast<std::size_t>(n));
        std::iota(permutation.begin(), permutation.end(), 0);

        for (std::int64_t hyperedge = 0; hyperedge < requested_r; ++hyperedge) {
            const int size = sample_hyperedge_size(
                distribution, n, k, s_min, s_max,
                largest_accepted_log_value, generator, poisson, normal,
                unit_interval, power_law_cdf);

            if (sampled_size_sum >
                std::numeric_limits<std::int64_t>::max() - size) {
                throw std::runtime_error("Sum of sampled hyperedge sizes exceeds int64");
            }
            sampled_size_sum += size;
            if (hyperedge == 0 || size < sampled_size_min) sampled_size_min = size;
            if (hyperedge == 0 || size > sampled_size_max) sampled_size_max = size;
            if (size < 2) continue;
            ++nontrivial_hyperedges;

            if (size < n) {
                for (int position = 0; position < size; ++position) {
                    const int selected = random_position(
                        generator,
                        std::uniform_int_distribution<int>::param_type(position, n - 1));
                    std::swap(permutation[position], permutation[selected]);
                }
            }

            const std::int64_t generated_pairs =
                static_cast<std::int64_t>(size) * (size - 1) / 2;
            if (pair_occurrences >
                std::numeric_limits<std::int64_t>::max() - generated_pairs) {
                throw std::runtime_error(
                    "Generated hyperedge pair occurrences exceed int64");
            }
            pair_occurrences += generated_pairs;

            const long double pair_probability =
                static_cast<long double>(size) *
                static_cast<long double>(size - 1) / ordered_vertex_pairs;
            if (pair_probability >= 1.0L) {
                log_pair_absence =
                    -std::numeric_limits<long double>::infinity();
            } else if (std::isfinite(log_pair_absence)) {
                log_pair_absence += std::log1p(-pair_probability);
            }

            const std::uint64_t added_entries =
                static_cast<std::uint64_t>(size - 1);
            for (int position = 0; position < size; ++position) {
                std::uint64_t& degree = adjacency_occurrences[permutation[position]];
                if (degree >
                    std::numeric_limits<std::uint64_t>::max() - added_entries) {
                    throw std::runtime_error(
                        "Generated adjacency occurrences exceed uint64");
                }
                degree += added_entries;
            }
        }
    }

    const long double edge_universe = ordered_vertex_pairs / 2.0L;
    long double estimated_distinct_edges = 0.0L;
    if (pair_occurrences > 0) {
        estimated_distinct_edges =
            edge_universe * (-std::expm1(log_pair_absence));
        estimated_distinct_edges = std::max(0.0L, estimated_distinct_edges);
        estimated_distinct_edges = std::min(
            estimated_distinct_edges,
            static_cast<long double>(pair_occurrences));
    }

    const long double pair_to_edge_ratio =
        estimated_distinct_edges > 0.0L
            ? static_cast<long double>(pair_occurrences) /
                  estimated_distinct_edges
            : 0.0L;
    constexpr long double sort_unique_ratio_limit = 2.0L;
    const bool use_sort_unique =
        pair_occurrences == 0 || pair_to_edge_ratio <= sort_unique_ratio_limit;

    adj.assign(static_cast<std::size_t>(n), {});
    if (use_sort_unique) {
        // Reserving the exact occurrence degree removes vector growth slack. The
        // capacity may include duplicates, but rho <= 2 bounds this storage while
        // avoiding a much larger exact edge hash.
        for (std::size_t v = 0; v < adj.size(); ++v) {
            if (adjacency_occurrences[v] >
                static_cast<std::uint64_t>(adj[v].max_size())) {
                throw std::runtime_error(
                    "Generated adjacency list exceeds addressable memory");
            }
            adj[v].reserve(
                static_cast<std::size_t>(adjacency_occurrences[v]));
        }
    }
    // The reserve calls have consumed the counts. Release this O(n) first-pass
    // array before allocating either the replay permutation or the edge hash.
    std::vector<std::uint64_t>().swap(adjacency_occurrences);

    if (use_sort_unique) {
        std::mt19937 generator(seed);
        std::poisson_distribution<long long> poisson(lambda);
        std::normal_distribution<double> normal(mu, sigma);
        std::uniform_real_distribution<long double> unit_interval(0.0L, 1.0L);
        std::uniform_int_distribution<int> random_position;
        std::vector<int> permutation(static_cast<std::size_t>(n));
        std::iota(permutation.begin(), permutation.end(), 0);

        // Reconstructing every RNG distribution as well as the engine matters:
        // normal_distribution may retain a cached normal variate. This setup
        // therefore replays exactly the hyperedges counted in pass one.
        for (std::int64_t hyperedge = 0; hyperedge < requested_r; ++hyperedge) {
            const int size = sample_hyperedge_size(
                distribution, n, k, s_min, s_max,
                largest_accepted_log_value, generator, poisson, normal,
                unit_interval, power_law_cdf);
            if (size < 2) continue;

            if (size < n) {
                for (int position = 0; position < size; ++position) {
                    const int selected = random_position(
                        generator,
                        std::uniform_int_distribution<int>::param_type(
                            position, n - 1));
                    std::swap(permutation[position], permutation[selected]);
                }
            }

            for (int first = 0; first < size; ++first) {
                const int u = permutation[first];
                for (int second = first + 1; second < size; ++second) {
                    const int v = permutation[second];
                    adj[u].push_back(v);
                    adj[v].push_back(u);
                }
            }
        }
    } else {
        // Expected m is known after pass one. Reserve a 10% margin (capped by P)
        // so the exact hash normally reaches completion without ever holding old
        // and new tables simultaneously.
        const long double hash_target_with_margin =
            std::ceil(estimated_distinct_edges * 1.10L) + 1024.0L;
        const std::uint64_t hash_target = static_cast<std::uint64_t>(
            std::min(static_cast<long double>(pair_occurrences),
                     hash_target_with_margin));

        FlatEdgeSet seen_edges;
        seen_edges.reserve_for_edges(hash_target);

        std::mt19937 generator(seed);
        std::poisson_distribution<long long> poisson(lambda);
        std::normal_distribution<double> normal(mu, sigma);
        std::uniform_real_distribution<long double> unit_interval(0.0L, 1.0L);
        std::uniform_int_distribution<int> random_position;
        std::vector<int> permutation(static_cast<std::size_t>(n));
        std::iota(permutation.begin(), permutation.end(), 0);

        for (std::int64_t hyperedge = 0; hyperedge < requested_r; ++hyperedge) {
            const int size = sample_hyperedge_size(
                distribution, n, k, s_min, s_max,
                largest_accepted_log_value, generator, poisson, normal,
                unit_interval, power_law_cdf);
            if (size < 2) continue;

            if (size < n) {
                for (int position = 0; position < size; ++position) {
                    const int selected = random_position(
                        generator,
                        std::uniform_int_distribution<int>::param_type(
                            position, n - 1));
                    std::swap(permutation[position], permutation[selected]);
                }
            }

            for (int first = 0; first < size; ++first) {
                const int u = permutation[first];
                for (int second = first + 1; second < size; ++second) {
                    const int v = permutation[second];
                    if (!seen_edges.insert(edge_key(u, v))) continue;
                    adj[u].push_back(v);
                    adj[v].push_back(u);
                }
            }
        }
    }

    // Both strategies require sorted lists. Only the low-duplication strategy
    // appended repeated occurrences, so only it performs unique compaction.
    std::int64_t adjacency_entries = 0;
    for (std::vector<int>& neighbors : adj) {
        std::sort(neighbors.begin(), neighbors.end());
        if (use_sort_unique) {
            const auto unique_end =
                std::unique(neighbors.begin(), neighbors.end());
            if (unique_end != neighbors.end()) {
                neighbors.erase(unique_end, neighbors.end());
                // erase() does not release the occurrence capacity. Rebuild only
                // lists that lost duplicates so the later construction does not
                // retain memory proportional to those removed occurrences.
                std::vector<int>(neighbors).swap(neighbors);
            }
        }
        if (neighbors.size() > static_cast<std::size_t>(
                                   std::numeric_limits<std::int64_t>::max() -
                                   adjacency_entries)) {
            throw std::runtime_error("Generated adjacency entries exceed int64");
        }
        adjacency_entries += static_cast<std::int64_t>(neighbors.size());
    }
    const std::int64_t distinct_edges = adjacency_entries / 2;

    metadata.name = configured_name;
    metadata.source = "synthetic";
    metadata.type = "hypergraph";
    metadata.parameters["distribution"] = distribution_name;
    metadata.parameters["n"] = requested_n;
    metadata.parameters["r"] = requested_r;
    metadata.parameters["seed"] = seed_value;
    if (distribution == HypergraphDistribution::KUniform) {
        metadata.parameters["k"] = static_cast<std::int64_t>(k);
    } else if (distribution == HypergraphDistribution::Poisson) {
        metadata.parameters["lambda"] = lambda;
    } else if (distribution == HypergraphDistribution::Lognormal) {
        metadata.parameters["mu"] = mu;
        metadata.parameters["sigma"] = sigma;
    } else {
        metadata.parameters["alpha"] = alpha;
        metadata.parameters["s_min"] = static_cast<std::int64_t>(s_min);
        metadata.parameters["s_max"] = static_cast<std::int64_t>(s_max);
    }

    metadata.generation_statistics["nontrivial_hyperedges"] =
        nontrivial_hyperedges;
    metadata.generation_statistics["sampled_size_sum"] = sampled_size_sum;
    metadata.generation_statistics["sampled_size_min"] =
        static_cast<std::int64_t>(sampled_size_min);
    metadata.generation_statistics["sampled_size_max"] =
        static_cast<std::int64_t>(sampled_size_max);
    metadata.generation_statistics["sampled_size_mean"] =
        requested_r == 0
            ? 0.0
            : static_cast<double>(sampled_size_sum) /
                  static_cast<double>(requested_r);
    metadata.generation_statistics["pair_occurrences"] = pair_occurrences;
    metadata.generation_statistics["estimated_distinct_edges"] =
        static_cast<double>(estimated_distinct_edges);
    metadata.generation_statistics["estimated_pair_to_edge_ratio"] =
        static_cast<double>(pair_to_edge_ratio);
    metadata.generation_statistics["deduplication_strategy"] =
        std::string(use_sort_unique ? "sort-unique" : "hash");
    metadata.generation_statistics["distinct_edges"] = distinct_edges;
}

}  // namespace cc_con
