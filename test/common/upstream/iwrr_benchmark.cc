// Usage: bazel run //test/common/upstream:iwrr_benchmark

#include <algorithm>
#include <memory>
#include <iostream>

#include "common/upstream/edf_scheduler.h"
#include "common/upstream/iwrr_scheduler.h"

#include "test/benchmark/main.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Upstream {
namespace {

/**
 * This is meant to be a quick hack, not an actual patch.
 */
class SchedulerTester {
public:
  SchedulerTester() {
  }

  // For simplicity, the objects being selected are just uints. The vectors returned by the setup
  // functions returns the objects to prevent expiry.

  struct ObjInfo {
    std::shared_ptr<uint32_t> val;
    double weight;
  };

  static std::vector<ObjInfo> setupUniqueWeights(
      RRScheduler<uint32_t>& sched, size_t num_objs, ::benchmark::State& state) {
    std::vector<ObjInfo> info;

    state.PauseTiming();
    for (uint32_t i = 0; i < num_objs; ++i) {
      ObjInfo oi;
      oi.val = std::make_shared<uint32_t>(i),
      oi.weight = static_cast<double>(i + 1),

      info.emplace_back(oi);
    } 

    std::random_shuffle(info.begin(), info.end());
    state.ResumeTiming();

    for (auto& oi : info) {
      sched.add(oi.weight, oi.val);
    }

    return info;
  }

  static void pickTest(RRScheduler<uint32_t>& sched,
                       size_t num_objs,
                       ::benchmark::State& state,
                       std::function<std::vector<ObjInfo>(RRScheduler<uint32_t>&)> setup) {

    std::vector<ObjInfo> obj_info;
    // Track number of times something's been picked.
    std::vector<size_t> pick_counts(num_objs, 0);

    for (auto _ : state) { // NOLINT: Silences warning about dead store
      if (obj_info.empty()){
        obj_info = setup(sched);
      }

      auto p = sched.pickAndAdd([&obj_info](const auto& i) {
        return obj_info[i].weight;
      });

      pick_counts[*p]++;
    }
  }
};

// -------------------------------------------------------------------------------------------------

void BM_UniqueWeightAddIWRR(::benchmark::State& state) {
  IWRRScheduler<uint32_t> iwrr;
  const size_t num_objs = state.range(0);
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    SchedulerTester::setupUniqueWeights(iwrr, num_objs, state);
  }
}
BENCHMARK(BM_UniqueWeightAddIWRR)
  ->Unit(::benchmark::kMicrosecond)
  ->RangeMultiplier(2)->Range(1<<5, 1<<16);

void BM_UniqueWeightPickIWRR(::benchmark::State& state) {
  IWRRScheduler<uint32_t> iwrr;
  const size_t num_objs = state.range(0);

  SchedulerTester::pickTest(iwrr, num_objs, state, [num_objs, &state](RRScheduler<uint32_t>& sched) {
    return SchedulerTester::setupUniqueWeights(sched, num_objs, state);
  });
}
BENCHMARK(BM_UniqueWeightPickIWRR)
  ->RangeMultiplier(2)->Range(1<<5, 1<<16);

void BM_UniqueWeightAddEdf(::benchmark::State& state) {
  EdfScheduler<uint32_t> edf;
  const size_t num_objs = state.range(0);
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    SchedulerTester::setupUniqueWeights(edf, num_objs, state);
  }
}
BENCHMARK(BM_UniqueWeightAddEdf)
  ->Unit(::benchmark::kMicrosecond)
  ->RangeMultiplier(2)->Range(1<<5, 1<<16);

void BM_UniqueWeightPickEdf(::benchmark::State& state) {
  EdfScheduler<uint32_t> edf;
  const size_t num_objs = state.range(0);

  SchedulerTester::pickTest(edf, num_objs, state, [num_objs, &state](RRScheduler<uint32_t>& sched) {
    return SchedulerTester::setupUniqueWeights(sched, num_objs, state);
  });
}
BENCHMARK(BM_UniqueWeightPickEdf)
  ->RangeMultiplier(2)->Range(1<<5, 1<<16);

// benchmarks
// - setup times
// - picks
// - peeks

} // namespace
} // namespace Upstream
} // namespace Envoy

