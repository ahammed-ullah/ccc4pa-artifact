#ifndef CC_CON_GRAPH_SOURCES_HPP
#define CC_CON_GRAPH_SOURCES_HPP

#include "cc_con/config.hpp"
#include "cc_con/graph.hpp"

#include <string>
#include <vector>

namespace cc_con {

// Source-specific functions stay concrete. Adding a model means
// one implementation file plus one explicit dispatch branch in graph.cpp.
void generate_hypergraph_graph(const Config& config,
                               const std::string& configured_name,
                               std::vector<std::vector<int>>& adj,
                               GraphMetadata& metadata);
void load_matrix_market_graph(const Config& config,
                              const std::string& configured_name,
                              std::vector<std::vector<int>>& adj,
                              GraphMetadata& metadata);
void load_cornell_simplices_graph(const Config& config,
                                  const std::string& configured_name,
                                  std::vector<std::vector<int>>& adj,
                                  GraphMetadata& metadata);
void load_cornell_dual_simplices_graph(const Config& config,
                                       const std::string& configured_name,
                                       std::vector<std::vector<int>>& adj,
                                       GraphMetadata& metadata);
void load_cornell_hyperedges_graph(const Config& config,
                                   const std::string& configured_name,
                                   std::vector<std::vector<int>>& adj,
                                   GraphMetadata& metadata);
void load_sx_tag_questions_graph(const Config& config,
                                 const std::string& configured_name,
                                 std::vector<std::vector<int>>& adj,
                                 GraphMetadata& metadata);

}  // namespace cc_con

#endif
