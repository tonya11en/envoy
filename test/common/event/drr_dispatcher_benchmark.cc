#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/event/dispatcher.h"

#include "source/common/common/assert.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/event/libevent.h"
#include "source/common/runtime/runtime_impl.h"

#include "test/benchmark/main.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "circllhist.h"

namespace Envoy {
namespace Event {
namespace {

class MockTenantScope : public ScopeTrackedObject {
public:
  explicit MockTenantScope(uintptr_t id) : id_(id) { drr_tenant_id_ = id_; }
  void dumpState(std::ostream& os, int) const override { os << "TenantScope-" << id_; }

private:
  uintptr_t id_;
};

class LatencyCollector {
public:
  LatencyCollector() : hist_(hist_fast_alloc(), hist_free) {}

  void record(double latency_us) { hist_insert(hist_.get(), latency_us, 1); }

  double p50() { return percentile(0.50); }
  double p90() { return percentile(0.90); }
  double p99() { return percentile(0.99); }
  double max() { return percentile(1.0); }

  void reset() { hist_clear(hist_.get()); }

  size_t count() const { return hist_sample_count(hist_.get()); }

private:
  double percentile(double p) {
    if (count() == 0) {
      return 0.0;
    }
    double result = 0.0;
    hist_approx_quantile(hist_.get(), &p, 1, &result);
    return result;
  }

  std::unique_ptr<histogram_t, decltype(&hist_free)> hist_;
};

void spinMicroseconds(uint32_t us) {
  if (us == 0) {
    return;
  }
  const std::chrono::microseconds duration(us);
  Event::TestTimeSystem::RealTimeBound bound(duration);
  while (bound.withinBound()) {
    // Busy wait to simulate deterministic CPU execution work.
    ::benchmark::ClobberMemory();
  }
}

struct BenchmarkParams {
  size_t num_well_behaved;
  size_t num_noisy;
  uint32_t quantum;
  uint32_t work_duration_us;
  bool enable_drr;
};

void runDispatcherBenchmark(::benchmark::State& state, const BenchmarkParams& params) {
  if (!Event::Libevent::Global::initialized()) {
    Event::Libevent::Global::initialize();
  }
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.drr_dispatcher_scheduling",
                               params.enable_drr ? "true" : "false"}});

  Api::ApiPtr api = Api::createApiForTest();
  DispatcherPtr dispatcher = api->allocateDispatcher("test_thread");

  if (params.enable_drr) {
    auto* dispatcher_impl = static_cast<DispatcherImpl*>(dispatcher.get());
    if (dispatcher_impl != nullptr) {
      dispatcher_impl->setDrrDefaultQuantumForTest(params.quantum);
    }
  }

  std::vector<std::unique_ptr<MockTenantScope>> wb_scopes;
  for (size_t i = 0; i < params.num_well_behaved; ++i) {
    wb_scopes.push_back(std::make_unique<MockTenantScope>(101 + i));
  }

  std::vector<std::unique_ptr<MockTenantScope>> noisy_scopes;
  for (size_t i = 0; i < params.num_noisy; ++i) {
    noisy_scopes.push_back(std::make_unique<MockTenantScope>(1001 + i));
  }

  LatencyCollector wb_collector;
  LatencyCollector noisy_collector;

  constexpr size_t WbCallbacksPerTenant = 10;
  constexpr size_t NoisyCallbacksPerTenant = 200;

  auto start_time = std::chrono::steady_clock::now();
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    // Enqueue noisy neighbor callbacks first so that under Legacy FIFO
    // they sit at the front of post_callbacks_ and create contention.
    for (size_t i = 0; i < params.num_noisy; ++i) {
      dispatcher->pushTrackedObject(noisy_scopes[i].get());
      for (size_t c = 0; c < NoisyCallbacksPerTenant; ++c) {
        auto t_post = std::chrono::steady_clock::now();
        dispatcher->post([t_post, &noisy_collector, &params]() {
          auto t_exec = std::chrono::steady_clock::now();
          double latency_us = std::chrono::duration<double, std::micro>(t_exec - t_post).count();
          noisy_collector.record(latency_us);
          spinMicroseconds(params.work_duration_us);
        });
      }
      dispatcher->popTrackedObject(noisy_scopes[i].get());
    }

    // Enqueue well-behaved tenant callbacks.
    for (size_t i = 0; i < params.num_well_behaved; ++i) {
      dispatcher->pushTrackedObject(wb_scopes[i].get());
      for (size_t c = 0; c < WbCallbacksPerTenant; ++c) {
        auto t_post = std::chrono::steady_clock::now();
        dispatcher->post([t_post, &wb_collector, &params]() {
          auto t_exec = std::chrono::steady_clock::now();
          double latency_us = std::chrono::duration<double, std::micro>(t_exec - t_post).count();
          wb_collector.record(latency_us);
          spinMicroseconds(params.work_duration_us);
        });
      }
      dispatcher->popTrackedObject(wb_scopes[i].get());
    }

    const size_t target_callbacks = wb_collector.count() + noisy_collector.count() +
                                    (params.num_well_behaved * WbCallbacksPerTenant) +
                                    (params.num_noisy * NoisyCallbacksPerTenant);
    // Run dispatcher non-blocking until all queued callbacks execute.
    while (wb_collector.count() + noisy_collector.count() < target_callbacks) {
      dispatcher->run(Dispatcher::RunType::NonBlock);
    }
  }
  auto end_time = std::chrono::steady_clock::now();

  double total_seconds = std::chrono::duration<double>(end_time - start_time).count();
  if (total_seconds <= 0.0) {
    total_seconds = 1e-6;
  }

  double wb_tput_ops_sec = static_cast<double>(wb_collector.count()) / total_seconds;
  double noisy_tput_ops_sec = static_cast<double>(noisy_collector.count()) / total_seconds;
  double fairness_ratio = wb_tput_ops_sec / std::max(1.0, noisy_tput_ops_sec);

  state.counters["wb_tput_ops_sec"] = wb_tput_ops_sec;
  state.counters["noisy_tput_ops_sec"] = noisy_tput_ops_sec;
  state.counters["wb_p50_us"] = wb_collector.p50();
  state.counters["wb_p90_us"] = wb_collector.p90();
  state.counters["wb_p99_us"] = wb_collector.p99();
  state.counters["wb_max_us"] = wb_collector.max();
  state.counters["noisy_p99_us"] = noisy_collector.p99();
  state.counters["fairness_ratio"] = fairness_ratio;

  state.SetItemsProcessed(static_cast<int64_t>(wb_collector.count() + noisy_collector.count()));
}

void BM_TenantScaling(::benchmark::State& state) {
  BenchmarkParams params;
  params.num_well_behaved = state.range(0);
  params.num_noisy = 1;
  params.quantum = 10;
  params.work_duration_us = 10;
  params.enable_drr = (state.range(1) == 1);
  runDispatcherBenchmark(state, params);
}

static void configureTenantScaling(::benchmark::internal::Benchmark* b) {
  for (int num_tenants : {2, 4, 8, 16, 32, 64}) {
    b->Args({num_tenants, 0});
    b->Args({num_tenants, 1});
  }
}
BENCHMARK(BM_TenantScaling)->Apply(configureTenantScaling);

void BM_NoisyNeighborScaling(::benchmark::State& state) {
  BenchmarkParams params;
  params.num_well_behaved = 10;
  params.num_noisy = state.range(0);
  params.quantum = 10;
  params.work_duration_us = 10;
  params.enable_drr = (state.range(1) == 1);
  runDispatcherBenchmark(state, params);
}

static void configureNoisyNeighborScaling(::benchmark::internal::Benchmark* b) {
  for (int num_noisy : {1, 2, 4, 8}) {
    b->Args({num_noisy, 0});
    b->Args({num_noisy, 1});
  }
}
BENCHMARK(BM_NoisyNeighborScaling)->Apply(configureNoisyNeighborScaling);

void BM_QuantumSensitivity(::benchmark::State& state) {
  BenchmarkParams params;
  params.num_well_behaved = 10;
  params.num_noisy = 1;
  params.quantum = state.range(0);
  params.work_duration_us = 10;
  params.enable_drr = (state.range(1) == 1);
  runDispatcherBenchmark(state, params);
}

static void configureQuantumSensitivity(::benchmark::internal::Benchmark* b) {
  for (int quantum : {1, 5, 10, 25, 50}) {
    b->Args({quantum, 0});
    b->Args({quantum, 1});
  }
}
BENCHMARK(BM_QuantumSensitivity)->Apply(configureQuantumSensitivity);

void BM_HeavyWorkload(::benchmark::State& state) {
  BenchmarkParams params;
  params.num_well_behaved = 10;
  params.num_noisy = 1;
  params.quantum = 10;
  params.work_duration_us = state.range(0);
  params.enable_drr = (state.range(1) == 1);
  runDispatcherBenchmark(state, params);
}

static void configureHeavyWorkload(::benchmark::internal::Benchmark* b) {
  for (int duration : {50, 100, 500, 1000}) {
    b->Args({duration, 0});
    b->Args({duration, 1});
  }
}
BENCHMARK(BM_HeavyWorkload)->Apply(configureHeavyWorkload);

} // namespace
} // namespace Event
} // namespace Envoy
