// Reference selection, block copying, and residual coding are adapted from
// Zuckerli. Copyright 2019 Google LLC, licensed under Apache License 2.0.
// See LICENSE and docs/upstream-fixes.md for the changes made here.

#include "encode.hpp"

#include "ccf_graph.hpp"
#include "codec.hpp"
#include "format.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace zk {
namespace {

using std::uint32_t;
using std::uint64_t;
using std::vector;

struct Token {
    uint32_t context;
    uint64_t value;
};

struct Work {
    vector<uint32_t> blocks;
    vector<uint32_t> residuals;
    vector<uint32_t> copied;
};

// A selected reference is at most W rows behind the current one. This ring
// holds only those input rows, not the full adjacency representation.
class RowWindow {
public:
    explicit RowWindow(const CcfGraph& graph) : graph_(graph), rows_(window + 1) {}

    const vector<uint32_t>& load(uint32_t vertex) {
        graph_.neighbors(vertex, rows_[vertex % rows_.size()]);
        return rows_[vertex % rows_.size()];
    }

    const vector<uint32_t>& previous(uint32_t vertex, uint32_t distance) const {
        return rows_[(vertex - distance) % rows_.size()];
    }

private:
    const CcfGraph& graph_;
    vector<vector<uint32_t>> rows_;
};

// Port of Zuckerli's ComputeBlocksAndResiduals with minimum copied run 0.
// Blocks alternate between copied and skipped runs of the reference row;
// the final copied run is implicit. Residuals are current-row neighbors that
// the reference cannot supply.
void split_row(const vector<uint32_t>& current, const vector<uint32_t>& reference,
               Work& work) {
    work.blocks.clear();
    work.residuals.clear();
    work.blocks.push_back(0);
    std::size_t c = 0, r = 0;
    bool copying = true;
    while (c < current.size() && r < reference.size()) {
        if (current[c] == reference[r]) {
            ++c;
            ++r;
            if (!copying) work.blocks.push_back(0);
            ++work.blocks.back();
            copying = true;
        } else if (current[c] < reference[r]) {
            work.residuals.push_back(current[c++]);
        } else {
            if (copying) work.blocks.push_back(0);
            ++work.blocks.back();
            ++r;
            copying = false;
        }
    }
    work.residuals.insert(work.residuals.end(), current.begin() + c,
                          current.end());
    if (r == reference.size() || !copying) work.blocks.pop_back();
}

template <typename Add>
void emit_blocks(const vector<uint32_t>& blocks,
                 const vector<uint32_t>& reference,
                 vector<uint32_t>& copied, Add add) {
    add(block_count_context, blocks.size());
    bool copy = true;
    std::size_t pos = 0;
    for (std::size_t j = 0; j < blocks.size(); ++j) {
        const auto length = blocks[j];
        const auto context = j == 0 ? block_context :
                             (j % 2 == 0 ? block_even_context : block_odd_context);
        add(context, length - (j != 0));
        if (copy) {
            copied.insert(copied.end(), reference.begin() + pos,
                          reference.begin() + pos + length);
        }
        pos += length;
        copy = !copy;
    }
    if (copy) {
        copied.insert(copied.end(), reference.begin() + pos, reference.end());
    }
}

template <typename Add, typename Undo>
void emit_residuals(const vector<uint32_t>& residuals, uint32_t vertex,
                    const vector<uint32_t>& copied, Add add, Undo undo) {
    uint64_t next = vertex;
    uint64_t last_delta = 0;
    std::size_t copied_pos = 0;
    std::size_t zero_run = 0;
    for (std::size_t j = 0; j < residuals.size(); ++j) {
        uint32_t context;
        if (j == 0) {
            context = first_residual_context(residuals.size());
            const auto diff = std::int64_t(residuals[j]) - vertex;
            last_delta = diff < 0 ? uint64_t(-2 * diff - 1) : uint64_t(2 * diff);
        } else {
            context = residual_context(last_delta);
            last_delta = residuals[j] - next;
            while (copied_pos < copied.size() && copied[copied_pos] < next) {
                ++copied_pos;
            }
            while (copied_pos < copied.size() &&
                   copied[copied_pos] < residuals[j]) {
                if (last_delta == 0) {
                    throw std::runtime_error("invalid copied/residual merge");
                }
                --last_delta;
                ++copied_pos;
            }
        }
        if (last_delta != 0) {
            if (zero_run >= zero_run_min) {
                for (std::size_t k = zero_run_min; k < zero_run; ++k) undo();
                add(rle_context, zero_run - zero_run_min);
            }
            zero_run = 0;
        } else {
            ++zero_run;
        }
        add(context, last_delta);
        next = uint64_t(residuals[j]) + 1;
    }
    if (zero_run >= zero_run_min) {
        for (std::size_t k = zero_run_min; k < zero_run; ++k) undo();
        add(rle_context, zero_run - zero_run_min);
    }
}

template <typename Add, typename Undo>
void emit_parts(const vector<uint32_t>& current,
                const vector<uint32_t>& reference_row, uint32_t vertex,
                uint32_t distance, Work& work, Add add, Undo undo) {
    work.copied.clear();
    if (distance == 0) {
        work.residuals.assign(current.begin(), current.end());
    } else {
        split_row(current, reference_row, work);
        emit_blocks(work.blocks, reference_row, work.copied, add);
    }
    emit_residuals(work.residuals, vertex, work.copied, add, undo);
}

// The published source estimates a candidate's zero-run saving using the
// base residual context. We retain that reference-choice heuristic. The
// histograms below count the actual tokens that will be stored.
float candidate_cost(const vector<uint32_t>& current,
                     const vector<uint32_t>& reference_row, uint32_t vertex,
                     uint32_t distance, const vector<float>& model,
                     Work& work) {
    float cost = 0;
    const auto add = [&](uint32_t context, uint64_t value) {
        const auto symbol = token(value);
        const auto extra_bits = value < 8 ? 0u :
            static_cast<unsigned>(63 - __builtin_clzll(value) - 1);
        cost += model[context * num_symbols + symbol] + extra_bits;
    };
    const auto undo = [&]() {
        cost -= model[residual_base_context * num_symbols];
    };
    emit_parts(current, reference_row, vertex, distance, work, add, undo);
    return cost;
}

vector<uint32_t> choose_references(const CcfGraph& graph,
                                   const vector<float>& model,
                                   uint32_t round) {
    vector<uint32_t> refs(graph.n(), 0);
    RowWindow rows(graph);
    Work work;
    const vector<uint32_t> empty;
    std::cerr << "Selecting references, round " << round << "/"
              << reference_rounds << "\n";
    for (uint32_t u = 0; u < graph.n(); ++u) {
        const auto& current = rows.load(u);
        float best = candidate_cost(current, empty, u, 0, model, work);
        for (uint32_t dist = 1; dist <= std::min(window, u); ++dist) {
            const auto& reference = rows.previous(u, dist);
            const float cost = candidate_cost(current, reference, u, dist,
                                              model, work);
            if (cost + 1e-6f < best) {
                best = cost;
                refs[u] = dist;
            }
        }
        if (u != 0 && u % 1000000 == 0) {
            std::cerr << "  " << u << "/" << graph.n() << " vertices\n";
        }
    }
    return refs;
}

void make_row_tokens(const vector<uint32_t>& current,
                     const vector<uint32_t>& reference_row, uint32_t vertex,
                     uint32_t distance, vector<Token>& tokens, Work& work) {
    tokens.clear();
    tokens.push_back({first_degree_context, current.size()});
    if (current.empty()) return;
    if (vertex != 0) tokens.push_back({reference_context(0), distance});
    const auto add = [&](uint32_t context, uint64_t value) {
        tokens.push_back({context, value});
    };
    const auto undo = [&]() {
        if (tokens.empty()) throw std::runtime_error("invalid zero run");
        tokens.pop_back();
    };
    emit_parts(current, reference_row, vertex, distance, work, add, undo);
}

template <typename Visit>
void visit_selected_rows(const CcfGraph& graph, const vector<uint32_t>& refs,
                         Visit visit) {
    RowWindow rows(graph);
    Work work;
    vector<Token> tokens;
    const vector<uint32_t> empty;
    for (uint32_t u = 0; u < graph.n(); ++u) {
        const auto& current = rows.load(u);
        const auto& reference = refs[u] ? rows.previous(u, refs[u]) : empty;
        make_row_tokens(current, reference, u, refs[u], tokens, work);
        visit(u, current, tokens);
    }
}

void add_histogram(vector<Histogram>& hist, const vector<Token>& tokens,
                   std::size_t start) {
    for (std::size_t j = start; j < tokens.size(); ++j) {
        const auto& t = tokens[j];
        ++hist[t.context][token(t.value)];
    }
}

} // namespace

EncodeStats encode_graph(const CcfGraph& graph, const std::string& basename) {
    vector<float> model(num_contexts * num_symbols, 1.0f);
    auto refs = choose_references(graph, model, 1);
    vector<Histogram> model_hist(num_contexts);
    visit_selected_rows(graph, refs,
        [&](uint32_t u, const vector<uint32_t>& current,
            const vector<Token>& tokens) {
            const std::size_t skip = 1 + (u != 0 && !current.empty());
            add_histogram(model_hist, tokens, skip);
        });
    for (uint32_t context = 0; context < num_contexts; ++context) {
        uint64_t total = 0;
        for (auto count : model_hist[context]) total += count;
        if (total == 0) continue;
        for (uint32_t symbol = 0; symbol < num_symbols; ++symbol) {
            const float count = std::max(0.1f,
                static_cast<float>(model_hist[context][symbol]));
            model[context * num_symbols + symbol] =
                std::log(static_cast<float>(total) / count);
        }
    }
    refs = choose_references(graph, model, 2);

    vector<Histogram> hist(num_contexts);
    uint64_t arcs = 0;
    visit_selected_rows(graph, refs,
        [&](uint32_t, const vector<uint32_t>& current,
            const vector<Token>& tokens) {
            arcs += current.size();
            add_histogram(hist, tokens, 0);
        });
    if (arcs != 2 * graph.m()) {
        throw std::runtime_error("CCF cover edge count differs from its header");
    }
    const auto tables = HuffmanTables::build(hist);
    const std::string graph_path = basename + ".zk";
    const std::string index_path = basename + ".zki";
    write_graph_header(graph_path, {graph.n(), graph.m(), graph.fingerprint()});
    std::ofstream output(graph_path, std::ios::binary | std::ios::app);
    if (!output) throw std::runtime_error("cannot append to " + graph_path);
    BitWriter writer(output, 8 * graph_header_bytes);
    tables.write(writer);
    vector<uint64_t> offsets;
    offsets.reserve(std::size_t(graph.n()) + 1);
    std::cerr << "Writing Huffman stream\n";
    visit_selected_rows(graph, refs,
        [&](uint32_t u, const vector<uint32_t>&,
            const vector<Token>& tokens) {
            offsets.push_back(writer.bits_written());
            for (const auto& t : tokens) {
                tables.encode(t.context, t.value, writer);
            }
            if (u != 0 && u % 1000000 == 0) {
                std::cerr << "  " << u << "/" << graph.n() << " vertices\n";
            }
        });
    offsets.push_back(writer.bits_written());
    writer.finish();
    output.close();
    if (!output) throw std::runtime_error("cannot finish " + graph_path);
    write_index(index_path, offsets);

    EncodeStats stats;
    stats.n = graph.n();
    stats.m = graph.m();
    stats.graph_bytes = std::filesystem::file_size(graph_path);
    stats.index_bytes = std::filesystem::file_size(index_path);
    stats.selected_references = std::count_if(refs.begin(), refs.end(),
                                               [](uint32_t ref) { return ref != 0; });
    return stats;
}

} // namespace zk
