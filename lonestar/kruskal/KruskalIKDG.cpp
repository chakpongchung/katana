#include "KruskalRuntime.h"

namespace kruskal {

class KruskalIKDG: public KruskalRuntime {

  struct LinkUpLoopIKDG {

    static const unsigned CHUNK_SIZE = DEFAULT_CHUNK_SIZE;

    VecLock& lockVec;
    VecRep& repVec;
    Accumulator& mstSum;
    Accumulator& linkUpIter;

    template <typename C>
    void operator () (const EdgeCtxt& e, C& ctx) {

      if (e.repSrc != e.repDst) {

        bool srcFail = !Galois::Runtime::owns (&lockVec[e.repSrc], Galois::MethodFlag::WRITE);
        bool dstFail = !Galois::Runtime::owns (&lockVec[e.repDst], Galois::MethodFlag::WRITE);

        if (srcFail && dstFail) {
          Galois::Runtime::signalConflict();

        } else { 

          if (srcFail) {
            linkUp_int (e.repDst, e.repSrc, repVec);

          } else {
            linkUp_int (e.repSrc, e.repDst, repVec);
          }

          linkUpIter += 1;
          mstSum += e.weight;

        }
      }
    }
  };

  struct UnionByRankIKDG {

    static const unsigned CHUNK_SIZE = DEFAULT_CHUNK_SIZE;

    VecLock& lockVec;
    VecRep& repVec;
    Accumulator& mstSum;
    Accumulator& linkUpIter;

    template <typename C>
    void operator () (const EdgeCtxt& e, C& ctx) {
      // int repSrc = kruskal::getRep_int (e.src, repVec);
      // int repDst = kruskal::getRep_int (e.dst, repVec);

      if (e.repSrc != e.repDst) {
        unionByRank_int (e.repSrc, e.repDst, repVec);

        linkUpIter += 1;
        mstSum += e.weight;
      }
    }
  };

  struct RunOrderedSpecOpt {

    template <typename R>
    void operator () (
        const R& edgeRange,
        VecLock& lockVec,
        VecRep& repVec,
        Accumulator& mstSum,
        Accumulator&  findIter, 
        Accumulator& linkUpIter) {

      FindLoopRuntime findLoop {lockVec, repVec, findIter};
      LinkUpLoopIKDG linkUpLoop {lockVec, repVec, mstSum, linkUpIter};

      Galois::Runtime::for_each_ordered_ikdg (
          edgeRange,
          Edge::Comparator (), findLoop, linkUpLoop,
          std::make_tuple (
            Galois::needs_custom_locking<> (),
            Galois::loopname ("kruskal-speculative-opt")));

    }
  };

  struct RunOrderedSpecBase {

    template <typename R>
    void operator () (
        const R& edgeRange,
        VecLock& lockVec,
        VecRep& repVec,
        Accumulator& mstSum,
        Accumulator&  findIter, 
        Accumulator& linkUpIter) {

      FindLoopRuntime findLoop {lockVec, repVec, findIter};
      UnionByRankIKDG linkUpLoop {lockVec, repVec, mstSum, linkUpIter};

      Galois::Runtime::for_each_ordered_ikdg (
          edgeRange,
          Edge::Comparator (), findLoop, linkUpLoop,
          std::make_tuple (
            Galois::loopname ("kruskal-speculative-base")));

    }
  };

  virtual const std::string getVersion () const { return "Parallel Kruskal using IKDG"; }

  virtual void runMST (const size_t numNodes, VecEdge& edges,
      size_t& mstWeight, size_t& totalIter) {

    if (useCustomLocking) {
      runMSTwithOrderedLoop (numNodes, edges, mstWeight, totalIter, RunOrderedSpecOpt {});
    } else {
      runMSTwithOrderedLoop (numNodes, edges, mstWeight, totalIter, RunOrderedSpecBase {});
    }
  }

};

} // end namespace kruskal

int main (int argc, char* argv[]) {
  kruskal::KruskalIKDG k;
  k.run (argc, argv);
  return 0;
}
