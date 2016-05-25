/** BFS -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2013, The University of Texas at Austin. All rights reserved.
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
 *
 * @section Description
 *
 * Compute BFS on distributed Galois.
 *
 * @author Gurbinder Gill <gurbinder533@gmail.com>
 */

#include <iostream>
#include <limits>
#include "Galois/Galois.h"
#include "Galois/gstl.h"
#include "Lonestar/BoilerPlate.h"
#include "Galois/Runtime/CompilerHelperFunctions.h"

#include "Galois/Dist/OfflineGraph.h"
#ifdef __GALOIS_VERTEX_CUT_GRAPH__
#include "Galois/Dist/vGraph.h"
#else
#include "Galois/Dist/hGraph.h"
#endif
#include "Galois/DistAccumulator.h"
#include "Galois/Runtime/Tracer.h"

#ifdef __GALOIS_HET_CUDA__
#include "Galois/Cuda/cuda_mtypes.h"
#include "gen_cuda.h"
struct CUDA_Context *cuda_ctx;

enum Personality {
   CPU, GPU_CUDA, GPU_OPENCL
};
std::string personality_str(Personality p) {
   switch (p) {
   case CPU:
      return "CPU";
   case GPU_CUDA:
      return "GPU_CUDA";
   case GPU_OPENCL:
      return "GPU_OPENCL";
   }
   assert(false&& "Invalid personality");
   return "";
}
#endif

static const char* const name = "BFS - Distributed Heterogeneous";
static const char* const desc = "BFS on Distributed Galois.";
static const char* const url = 0;

namespace cll = llvm::cl;
static cll::opt<std::string> inputFile(cll::Positional, cll::desc("<input file (Transpose graph)>"), cll::Required);
#ifdef __GALOIS_VERTEX_CUT_GRAPH__
static cll::opt<std::string> partFolder("partFolder", cll::desc("path to partitionFolder"), cll::init(""));
#endif
static cll::opt<unsigned int> maxIterations("maxIterations", cll::desc("Maximum iterations: Default 1024"), cll::init(1024));
static cll::opt<unsigned int> src_node("srcNodeId", cll::desc("ID of the source node"), cll::init(0));
static cll::opt<bool> verify("verify", cll::desc("Verify ranks by printing to 'page_ranks.#hid.csv' file"), cll::init(false));
#ifdef __GALOIS_HET_CUDA__
static cll::opt<int> gpudevice("gpu", cll::desc("Select GPU to run on, default is to choose automatically"), cll::init(-1));
static cll::opt<Personality> personality("personality", cll::desc("Personality"),
      cll::values(clEnumValN(CPU, "cpu", "Galois CPU"), clEnumValN(GPU_CUDA, "gpu/cuda", "GPU/CUDA"), clEnumValN(GPU_OPENCL, "gpu/opencl", "GPU/OpenCL"), clEnumValEnd),
      cll::init(CPU));
static cll::opt<std::string> personality_set("pset", cll::desc("String specifying personality for each host. 'c'=CPU,'g'=GPU/CUDA and 'o'=GPU/OpenCL"), cll::init(""));
static cll::opt<unsigned> scalegpu("scalegpu", cll::desc("Scale GPU workload w.r.t. CPU, default is proportionally equal workload to CPU and GPU (1)"), cll::init(1));
static cll::opt<unsigned> scalecpu("scalecpu", cll::desc("Scale CPU workload w.r.t. GPU, default is proportionally equal workload to CPU and GPU (1)"), cll::init(1));
#endif

const unsigned int infinity = std::numeric_limits<unsigned int>::max()/4;
unsigned iteration;

struct NodeData {
  unsigned int dist_current;
};

#ifdef __GALOIS_VERTEX_CUT_GRAPH__
typedef vGraph<NodeData, void> Graph;
#else
typedef hGraph<NodeData, void> Graph;
#endif
typedef typename Graph::GraphNode GNode;

struct InitializeGraph {
  cll::opt<unsigned int> &local_src_node;
  unsigned long local_infinity;
  Graph *graph;

  InitializeGraph(cll::opt<unsigned int> &_src_node, unsigned int _infinity, Graph* _graph) : local_src_node(_src_node), local_infinity(_infinity), graph(_graph){}

  void static go(Graph& _graph) {
    struct SyncerPull_0 {
      static unsigned int extract(uint32_t node_id, const struct NodeData & node) {
#ifdef __GALOIS_HET_CUDA__
        if (personality == GPU_CUDA) return get_node_dist_current_cuda(cuda_ctx, node_id);
        assert (personality == CPU);
#endif
        return node.dist_current;
      }
      static void setVal (uint32_t node_id, struct NodeData & node, unsigned int y) {
#ifdef __GALOIS_HET_CUDA__
        if (personality == GPU_CUDA) set_node_dist_current_cuda(cuda_ctx, node_id, y);
        else if (personality == CPU)
#endif
          node.dist_current = y;
      }
      typedef unsigned int ValTy;
    };

    #ifdef __GALOIS_HET_CUDA__
    	if (personality == GPU_CUDA) {
    		InitializeGraph_cuda(src_node, infinity, cuda_ctx);
    	} else if (personality == CPU)
    #endif
    Galois::do_all(_graph.begin(), _graph.end(), InitializeGraph {src_node, infinity, &_graph}, Galois::loopname("InitGraph"));

    _graph.sync_pull<SyncerPull_0>();
  }

  void operator()(GNode src) const {
    NodeData& sdata = graph->getData(src);
    sdata.dist_current = (graph->getGID(src) == local_src_node) ? 0 : local_infinity;
  }
};

struct BFS {
  Graph* graph;
  static Galois::DGAccumulator<int> DGAccumulator_accum;

  BFS(Graph* _graph) : graph(_graph){}
  void static go(Graph& _graph){
    iteration = 0;
    do{
      DGAccumulator_accum.reset();

#ifdef __GALOIS_VERTEX_CUT_GRAPH__
      	struct Syncer_0 {
      		static unsigned int extract(uint32_t node_id, const struct NodeData & node) {
      		#ifdef __GALOIS_HET_CUDA__
      			if (personality == GPU_CUDA) return get_node_dist_current_cuda(cuda_ctx, node_id);
      			assert (personality == CPU);
      		#endif
      			return node.dist_current;
      		}
      		static void reduce (uint32_t node_id, struct NodeData & node, unsigned int y) {
      		#ifdef __GALOIS_HET_CUDA__
      			if (personality == GPU_CUDA) min_node_dist_current_cuda(cuda_ctx, node_id, y);
      			else if (personality == CPU)
      		#endif
      				{ if (node.dist_current > y) node.dist_current = y; }
      		}
      		static void reset (uint32_t node_id, struct NodeData & node ) {
      		#ifdef __GALOIS_HET_CUDA__
      			if (personality == GPU_CUDA) set_node_dist_current_cuda(cuda_ctx, node_id, std::numeric_limits<unsigned int>::max()/4);
      			else if (personality == CPU)
      		#endif
      				{node.dist_current = std::numeric_limits<unsigned int>::max()/4; }
      		}
      		typedef unsigned int ValTy;
      	};
#endif
      	struct SyncerPull_0 {
      		static unsigned int extract(uint32_t node_id, const struct NodeData & node) {
      		#ifdef __GALOIS_HET_CUDA__
      			if (personality == GPU_CUDA) return get_node_dist_current_cuda(cuda_ctx, node_id);
      			assert (personality == CPU);
      		#endif
      			return node.dist_current;
      		}
      		static void setVal (uint32_t node_id, struct NodeData & node, unsigned int y) {
      		#ifdef __GALOIS_HET_CUDA__
      			if (personality == GPU_CUDA) set_node_dist_current_cuda(cuda_ctx, node_id, y);
      			else if (personality == CPU)
      		#endif
      				node.dist_current = y;
      		}
      		typedef unsigned int ValTy;
      	};
      #ifdef __GALOIS_HET_CUDA__
      	if (personality == GPU_CUDA) {
      		int __retval = 0;
      		BFS_cuda(__retval, cuda_ctx);
      		DGAccumulator_accum += __retval;
      	} else if (personality == CPU)
      #endif
      Galois::do_all(_graph.begin(), _graph.end(), BFS { &_graph }, Galois::loopname("bfs"), Galois::write_set("sync_pull", "this->graph", "struct NodeData &", "struct NodeData &", "dist_current" , "unsigned int"));
#ifdef __GALOIS_VERTEX_CUT_GRAPH__
      _graph.sync_push<Syncer_0>();
#endif
      _graph.sync_pull<SyncerPull_0>();
      

     ++iteration;
    }while((iteration < maxIterations) && DGAccumulator_accum.reduce());
  }

  void operator()(GNode src) const {
    NodeData& snode = graph->getData(src);

    unsigned int current_min = snode.dist_current;
    for (auto jj = graph->edge_begin(src); jj != graph->edge_end(src); ++jj) {
      GNode dst = graph->getEdgeDst(jj);
      auto& dnode = graph->getData(dst);
      unsigned int new_dist;
      new_dist = dnode.dist_current + 1;
      if(current_min > new_dist){
        current_min = new_dist;
      }
    }

    if(snode.dist_current > current_min){
      snode.dist_current = current_min;
      DGAccumulator_accum += 1;
    }
  }
};
Galois::DGAccumulator<int>  BFS::DGAccumulator_accum;

int main(int argc, char** argv) {
  try {
    LonestarStart(argc, argv, name, desc, url);
    auto& net = Galois::Runtime::getSystemNetworkInterface();
    Galois::Timer T_total, T_graph_load, T_init, T_bfs1, T_bfs2, T_bfs3;

    std::vector<unsigned> scalefactor;
#ifdef __GALOIS_HET_CUDA__
    const unsigned my_host_id = Galois::Runtime::getHostID();
    int gpu_device = gpudevice;
    //Parse arg string when running on multiple hosts and update/override personality
    //with corresponding value.
    if (personality_set.length() == Galois::Runtime::NetworkInterface::Num) {
      switch (personality_set.c_str()[my_host_id]) {
      case 'g':
        personality = GPU_CUDA;
        break;
      case 'o':
        assert(0);
        personality = GPU_OPENCL;
        break;
      case 'c':
      default:
        personality = CPU;
        break;
      }
#ifdef __GALOIS_SINGLE_HOST_MULTIPLE_GPUS__
      if (gpu_device == -1) {
        gpu_device = 0;
        for (unsigned i = 0; i < my_host_id; ++i) {
          if (personality_set.c_str()[i] != 'c') ++gpu_device;
        }
      }
#endif
      for (unsigned i=0; i<personality_set.length(); ++i) {
        if (personality_set.c_str()[i] == 'c') 
          scalefactor.push_back(scalecpu);
        else
          scalefactor.push_back(scalegpu);
      }
    }
#endif

    T_total.start();

    T_graph_load.start();
#ifdef __GALOIS_VERTEX_CUT_GRAPH__
    Graph hg(inputFile, partFolder, net.ID, net.Num, scalefactor);
#else
    Graph hg(inputFile, net.ID, net.Num, scalefactor);
#endif
#ifdef __GALOIS_HET_CUDA__
    if (personality == GPU_CUDA) {
      cuda_ctx = get_CUDA_context(my_host_id);
      if (!init_CUDA_context(cuda_ctx, gpu_device))
        return -1;
      MarshalGraph m = hg.getMarshalGraph(my_host_id);
      load_graph_CUDA(cuda_ctx, m);
    } else if (personality == GPU_OPENCL) {
      //Galois::OpenCL::cl_env.init(cldevice.Value);
    }
#endif
    T_graph_load.stop();

    std::cout << "InitializeGraph::go called\n";
    T_init.start();
    InitializeGraph::go(hg);
    T_init.stop();

    std::cout << "BFS::go run1 called  on " << net.ID << "\n";
    T_bfs1.start();
      BFS::go(hg);
    T_bfs1.stop();

    Galois::Runtime::getHostBarrier().wait();
    InitializeGraph::go(hg);

    std::cout << "BFS::go run2 called  on " << net.ID << "\n";
    T_bfs2.start();
      BFS::go(hg);
    T_bfs2.stop();

    Galois::Runtime::getHostBarrier().wait();
    InitializeGraph::go(hg);

    std::cout << "BFS::go run3 called  on " << net.ID << "\n";
    T_bfs3.start();
      BFS::go(hg);
    T_bfs3.stop();

   T_total.stop();

    auto mean_time = (T_bfs1.get() + T_bfs2.get() + T_bfs3.get())/3;

    std::cout << "[" << net.ID << "]" << " Total Time : " << T_total.get() << " Graph : " << T_graph_load.get() << " Init : " << T_init.get() << " bfs1 : " << T_bfs1.get() << " bfs2 : " << T_bfs2.get() << " bfs3 : " << T_bfs3.get() <<" bfs mean time (" << iteration << " iterations) : " << mean_time << "(msec)\n\n";

    // Verify
    if(verify){
#ifdef __GALOIS_HET_CUDA__
      if (personality == CPU) { 
#endif
        for(auto ii = hg.begin(); ii != hg.end(); ++ii) {
          Galois::Runtime::printOutput("% %\n", hg.getGID(*ii), hg.getData(*ii).dist_current);
        }
#ifdef __GALOIS_HET_CUDA__
      } else if(personality == GPU_CUDA)  {
        for(auto ii = hg.begin(); ii != hg.end(); ++ii) {
          Galois::Runtime::printOutput("% %\n", hg.getGID(*ii), get_node_dist_current_cuda(cuda_ctx, *ii));
        }
      }
#endif
    }

    return 0;
  } catch(const char* c) {
    std::cerr << "Error: " << c << "\n";
      return 1;
  }
}
