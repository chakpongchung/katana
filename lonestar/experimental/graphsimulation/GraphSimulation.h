/*
 * This file belongs to the Galois project, a C++ library for exploiting parallelism.
 * The code is being released under the terms of the 3-Clause BSD License (a
 * copy is located in LICENSE.txt at the top-level directory).
 *
 * Copyright (C) 2018, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 */

/**
 * @file GraphSimulation.h
 *
 * Contains definitions of graph related structures for graph simluation in
 * Galois, most notably the AttributedGraph structure.
 */

#include "galois/Galois.h"
#include "galois/graphs/LCGraph.h"

#include <string>

/**
 * Node data type.
 */
struct Node {
  //! Label on node. Maximum of 32 node labels.
  uint32_t label;
  //! ID of node
  uint32_t id;
  //! Matched status of node represented in bits. Max of 64 matched in query
  //! graph.
  //! @todo make matched a dynamic bitset
  uint64_t matched;
};

/**
 * Edge data type
 */
struct EdgeData {
  //! Label on the edge (like the type of action). Max of 32 edge labels.
  uint32_t label;
  //! Timestamp of action the edge represents. Range is limited.
  uint64_t timestamp;
  //! Matched status on the edge represented in bits. Max of 64 matched in
  //! query graph.
  uint64_t matched;
  /**
   * Constructor for edge data. Defaults to unmatched.
   * @param l Type of action this edge represents
   * @param t Timestamp of action
   */
  EdgeData(uint32_t l, uint64_t t) : label(l), timestamp(t), matched(0) {}
};

/**
 * Represents a matched node.
 */
struct MatchedNode {
  //! ID of matched node
  uint32_t id;
  // const char* label;
  //! Name for matched node
  const char* name;
};

/**
 * Represents a matched edge.
 */
struct MatchedEdge {
  //! timestamp on edge
  uint64_t timestamp;
  //! label on edge
  const char* label;
  //! actor of edge
  MatchedNode caused_by;
  //! target of edge's action
  MatchedNode acted_on;
};

//! Time-limit of consecutive events (inclusive)
struct EventLimit {
  bool valid;
  uint64_t time; // inclusive
  EventLimit() : valid(false) {}
};

//! Time-span of all events (inclusive)
struct EventWindow {
  bool valid;
  uint64_t startTime; // inclusive
  uint64_t endTime;   // inclusive
  EventWindow() : valid(false) {}
};

//! Graph typedef
using Graph = galois::graphs::LC_CSR_Graph<Node, EdgeData>::
                with_no_lockable<true>::type::with_numa_alloc<true>::type;
//! Graph node typedef
using GNode = Graph::GraphNode;

/**
 * Wrapped graph that contains metadata maps explaining what the compressed
 * data stored in the graph proper mean. For example, instead of storing
 * node types directly on the Graph, an int (which maps to a node type)
 * is stored on the node data.
 */
struct AttributedGraph {
  //! Graph structure class
  Graph graph;
  std::vector<std::string> nodeLabelNames;      //!< maps ID to Name
  std::map<std::string, uint32_t> nodeLabelIDs; //!< maps Name to ID
  std::vector<std::string> edgeLabelNames;      //!< maps ID to Name
  std::map<std::string, uint32_t> edgeLabelIDs; //!< maps Name to ID
  //! maps node UUID/ID to index/GraphNode
  std::map<uint32_t, uint32_t> nodeIndices;
  //! actual names of nodes
  std::vector<std::string> nodeNames; // cannot use LargeArray because serialize
                                      // does not do deep-copy
  // custom attributes: maps from an attribute name to a vector that contains
  // the attribute for each node/edge
  //! attribute name (example: file) to vector of names for that attribute
  std::map<std::string, std::vector<std::string>> nodeAttributes;
  //! edge attribute name to vector of names for that attribute
  std::map<std::string, std::vector<std::string>> edgeAttributes;
};

/**
 * @todo doxygen
 */
void runGraphSimulation(Graph& queryGraph, Graph& dataGraph, EventLimit limit,
                        EventWindow window, bool queryNodeHasMoreThan2Edges);
/**
 * @todo doxygen
 */
void matchNodeWithRepeatedActions(Graph& graph, uint32_t nodeLabel,
                                  uint32_t action, EventWindow window);
/**
 * @todo doxygen
 */
void matchNodeWithTwoActions(Graph& graph, uint32_t nodeLabel, uint32_t action1,
                             uint32_t dstNodeLabel1, uint32_t action2,
                             uint32_t dstNodeLabel2, EventWindow window);
/**
 * @todo doxygen
 */
void matchNeighbors(Graph& graph, Graph::GraphNode node, uint32_t nodeLabel,
                    uint32_t action, uint32_t neighborLabel,
                    EventWindow window);

/**
 * Get the number of matched nodes in the graph.
 * @param graph Graph to count matched nodes in
 * @returns Number of matched nodes in the graph
 */
size_t countMatchedNodes(Graph& graph);
/**
 * Get the number of matched neighbors of a node in the graph.
 * @warning Right now it literally does the same thing as countMatchedNodes
 */
size_t countMatchedNeighbors(Graph& graph, Graph::GraphNode node);
/**
 * Get the number of matched edges in the graph.
 * @param graph Graph to count matched edges in
 * @returns Number of matched edges in the graph
 */
size_t countMatchedEdges(Graph& graph);
/**
 * Get the number of matched edges of a particular node in the graph.
 * @param graph Graph to count matched edges in
 * @returns Number of matched edges in the graph
 */
size_t countMatchedNeighborEdges(Graph& graph, Graph::GraphNode node);
