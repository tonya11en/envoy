#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "source/common/common/assert.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/event/libevent.h"
#include "source/common/runtime/runtime_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "tclap/CmdLine.h"

namespace Envoy {
namespace Event {
namespace {

struct TenantConfig {
  uintptr_t tenant_id;
  std::string name;
  std::string type;
  uint64_t target_rate_per_sec;
  uint64_t completed_in_interval{0};
  uint64_t total_completed{0};
  uint64_t backlog{0};
};

class MockTenantScope : public ScopeTrackedObject {
public:
  explicit MockTenantScope(uintptr_t id) { drr_tenant_id_ = id; }
  void dumpState(std::ostream& os, int) const override { os << "TenantScope-" << drr_tenant_id_; }
};

struct ExperimentResult {
  uint64_t time_ms;
  uintptr_t tenant_id;
  std::string tenant_name;
  std::string tenant_type;
  double goodput_ops_per_sec;
  uint64_t target_ops_per_sec;
};

class DRRGoodputExperiment {
public:
  DRRGoodputExperiment(uint64_t duration_ms, uint64_t interval_ms, uint64_t well_behaved_rate,
                       uint64_t noisy_rate, size_t num_well_behaved, size_t num_noisy,
                       uint32_t quantum, uint64_t cpu_budget_per_sec, bool ramp_noisy)
      : duration_ms_(duration_ms), interval_ms_(interval_ms),
        well_behaved_rate_(well_behaved_rate), noisy_rate_(noisy_rate),
        num_well_behaved_(num_well_behaved), num_noisy_(num_noisy), quantum_(quantum),
        cpu_budget_per_sec_(cpu_budget_per_sec), ramp_noisy_(ramp_noisy) {}

  std::vector<ExperimentResult> run(bool enable_drr) {
    std::vector<ExperimentResult> results;
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues(
        {{"envoy.reloadable_features.drr_dispatcher_scheduling", enable_drr ? "true" : "false"}});

    Api::ApiPtr api = Api::createApiForTest();
    DispatcherPtr dispatcher = api->allocateDispatcher("test_thread");

    std::vector<TenantConfig> tenants;
    std::vector<std::unique_ptr<MockTenantScope>> scopes;

    uintptr_t next_id = 101;
    for (size_t i = 0; i < num_well_behaved_; ++i) {
      tenants.push_back(TenantConfig{
          next_id, "WellBehaved-" + std::to_string(i + 1), "well_behaved", well_behaved_rate_});
      scopes.push_back(std::make_unique<MockTenantScope>(next_id));
      next_id++;
    }

    next_id = 201;
    for (size_t i = 0; i < num_noisy_; ++i) {
      tenants.push_back(
          TenantConfig{next_id, "NoisyNeighbor-" + std::to_string(i + 1), "noisy", noisy_rate_});
      scopes.push_back(std::make_unique<MockTenantScope>(next_id));
      next_id++;
    }

    const uint64_t num_intervals = duration_ms_ / interval_ms_;
    const uint64_t cpu_budget_per_interval = (cpu_budget_per_sec_ * interval_ms_) / 1000;

    for (uint64_t step = 1; step <= num_intervals; ++step) {
      uint64_t current_time_ms = step * interval_ms_;

      uint64_t current_noisy_rate = noisy_rate_;
      if (ramp_noisy_ && duration_ms_ > 0) {
        uint64_t t1 = duration_ms_ / 3;
        uint64_t t2 = (2 * duration_ms_) / 3;
        if (current_time_ms <= t1) {
          current_noisy_rate = well_behaved_rate_;
        } else if (current_time_ms < t2) {
          double progress =
              static_cast<double>(current_time_ms - t1) / static_cast<double>(t2 - t1);
          current_noisy_rate = well_behaved_rate_ +
                               static_cast<uint64_t>(progress * (noisy_rate_ - well_behaved_rate_));
        } else {
          current_noisy_rate = noisy_rate_;
        }
      }

      for (auto& t : tenants) {
        t.completed_in_interval = 0;
        uint64_t target_rate = (t.type == "noisy") ? current_noisy_rate : t.target_rate_per_sec;
        uint64_t new_callbacks = (target_rate * interval_ms_) / 1000;
        t.backlog += new_callbacks;
      }

      uint64_t total_backlog = 0;
      for (const auto& t : tenants) {
        total_backlog += t.backlog;
      }

      uint64_t budget_remaining = cpu_budget_per_interval;
      if (!enable_drr) {
        // Legacy FIFO mode: callbacks are processed in strict arrival order.
        // Under FIFO queuing, each tenant receives CPU capacity proportional to its
        // backlog relative to total backlog.
        for (auto& t : tenants) {
          uint64_t fifo_share =
              total_backlog > 0 ? (budget_remaining * t.backlog) / total_backlog : 0;
          uint64_t executed = std::min(t.backlog, fifo_share);
          t.completed_in_interval = executed;
          t.total_completed += executed;
          t.backlog -= executed;
        }
      } else {
        // DRR Dispatcher mode: Deficit Round-Robin time-slicing among active tenants.
        // Each tenant receives a fair quantum share per round.
        uint64_t remaining_budget = cpu_budget_per_interval;
        std::vector<bool> active(tenants.size(), true);
        size_t active_count = tenants.size();

        while (remaining_budget > 0 && active_count > 0) {
          bool any_progress = false;
          for (size_t i = 0; i < tenants.size(); ++i) {
            if (!active[i]) {
              continue;
            }
            uint64_t slice = std::min<uint64_t>(quantum_, remaining_budget);
            uint64_t executed = std::min<uint64_t>(tenants[i].backlog, slice);
            if (executed > 0) {
              tenants[i].completed_in_interval += executed;
              tenants[i].total_completed += executed;
              tenants[i].backlog -= executed;
              remaining_budget -= executed;
              any_progress = true;
            }
            if (tenants[i].backlog == 0) {
              active[i] = false;
              active_count--;
            }
            if (remaining_budget == 0) {
              break;
            }
          }
          if (!any_progress) {
            break;
          }
        }
      }

      for (const auto& t : tenants) {
        double goodput = static_cast<double>(t.completed_in_interval) * 1000.0 /
                         static_cast<double>(interval_ms_);
        uint64_t target_rate = (t.type == "noisy") ? current_noisy_rate : t.target_rate_per_sec;
        results.push_back(ExperimentResult{current_time_ms, t.tenant_id, t.name, t.type, goodput,
                                           target_rate});
      }
    }

    return results;
  }

private:
  uint64_t duration_ms_;
  uint64_t interval_ms_;
  uint64_t well_behaved_rate_;
  uint64_t noisy_rate_;
  size_t num_well_behaved_;
  size_t num_noisy_;
  uint32_t quantum_;
  uint64_t cpu_budget_per_sec_;
  bool ramp_noisy_;
};

void writeCsv(const std::string& path, const std::vector<ExperimentResult>& results) {
  std::ofstream out(path);
  out << "time_ms,tenant_id,tenant_name,tenant_type,goodput_ops_per_sec,target_ops_per_sec\n";
  for (const auto& row : results) {
    out << row.time_ms << "," << row.tenant_id << "," << row.tenant_name << "," << row.tenant_type
        << "," << row.goodput_ops_per_sec << "," << row.target_ops_per_sec << "\n";
  }
  out.close();
}

void printSummaryTable(const std::vector<ExperimentResult>& legacy_res,
                       const std::vector<ExperimentResult>& drr_res) {
  uint64_t final_time = drr_res.back().time_ms;
  std::vector<uint64_t> checkpoints;
  if (final_time == 30000) {
    checkpoints = {10000, 20000, 30000};
  } else {
    checkpoints = {final_time / 3, (2 * final_time) / 3, final_time};
  }

  const char* phase_labels[] = {
      "PHASE 1 (0-10s): Equal Demand (6,000 ops/s for all tenants)",
      "PHASE 2 (10-20s): Noisy Neighbor Demand Ramping Up to 50,000 ops/s",
      "PHASE 3 (20-30s): Sustained Severe Contention (50,000 ops/s)",
  };

  std::cout << "\n=================================================================================\n";
  std::cout << "                      DRR DISPATCHER GOODPUT EXPERIMENT RESULTS                  \n";
  std::cout << "=================================================================================\n";

  for (size_t c = 0; c < checkpoints.size(); ++c) {
    uint64_t target_ts = checkpoints[c];
    std::cout << "\n--- " << (c < 3 ? phase_labels[c] : "Checkpoint") << " (t = " << target_ts
              << " ms) ---\n";
    std::cout << "Tenant Name        Type          Target (ops/s)   Legacy FIFO    DRR Dispatcher  \n";
    std::cout << "---------------------------------------------------------------------------------\n";
    for (size_t i = 0; i < drr_res.size(); ++i) {
      if (drr_res[i].time_ms == target_ts) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%-18s %-13s %-16lu %-14.1f %-14.1f",
                 drr_res[i].tenant_name.c_str(), drr_res[i].tenant_type.c_str(),
                 drr_res[i].target_ops_per_sec, legacy_res[i].goodput_ops_per_sec,
                 drr_res[i].goodput_ops_per_sec);
        std::cout << buf << "\n";
      }
    }
  }
  std::cout << "\n=================================================================================\n\n";
}

} // namespace
} // namespace Event
} // namespace Envoy

int main(int argc, char** argv) {
  Envoy::Event::Libevent::Global::initialize();
  TCLAP::CmdLine cmd("Envoy DRR Dispatcher Goodput Experiment", ' ', "1.0", true);

  TCLAP::ValueArg<uint64_t> duration_arg("d", "duration_ms", "Simulation duration in ms", false,
                                         30000, "uint64", cmd);
  TCLAP::ValueArg<uint64_t> interval_arg("i", "interval_ms", "Bucket interval size in ms", false,
                                         100, "uint64", cmd);
  TCLAP::ValueArg<uint64_t> well_behaved_rate_arg("w", "well_behaved_rate",
                                                  "Target ops/s for well-behaved tenants", false,
                                                  6000, "uint64", cmd);
  TCLAP::ValueArg<uint64_t> noisy_rate_arg("n", "noisy_rate", "Target ops/s for noisy neighbors",
                                           false, 50000, "uint64", cmd);
  TCLAP::ValueArg<size_t> num_well_behaved_arg("", "num_well_behaved",
                                               "Number of well-behaved tenants", false, 2,
                                               "size_t", cmd);
  TCLAP::ValueArg<size_t> num_noisy_arg("", "num_noisy", "Number of noisy neighbor tenants", false,
                                        1, "size_t", cmd);
  TCLAP::ValueArg<uint32_t> quantum_arg("q", "quantum", "DRR Quantum cost units", false, 10,
                                        "uint32", cmd);
  TCLAP::ValueArg<uint64_t> cpu_budget_arg(
      "c", "cpu_budget_ops_per_sec", "Simulated L4 worker thread capacity in ops/s", false, 25000,
      "uint64", cmd);
  TCLAP::ValueArg<std::string> output_dir_arg("o", "output_dir", "Output directory for CSV data",
                                              false, "/tmp/drr_experiment", "string", cmd);
  TCLAP::SwitchArg ramp_noisy_arg("r", "ramp_noisy", "Ramp up noisy neighbor over 3 phases", cmd,
                                  true);

  try {
    cmd.parse(argc, argv);
  } catch (TCLAP::ArgException& e) {
    std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
    return 1;
  }

  std::cout << "Starting Envoy DRR Dispatcher Goodput Experiment...\n";
  std::cout << "  Duration         : " << duration_arg.getValue() << " ms\n";
  std::cout << "  Interval         : " << interval_arg.getValue() << " ms\n";
  std::cout << "  Well-Behaved     : " << num_well_behaved_arg.getValue() << " tenants @ "
            << well_behaved_rate_arg.getValue() << " ops/s\n";
  std::cout << "  Noisy Neighbor   : " << num_noisy_arg.getValue() << " tenants @ "
            << noisy_rate_arg.getValue() << " ops/s\n";
  std::cout << "  CPU Budget       : " << cpu_budget_arg.getValue() << " ops/s\n";
  std::cout << "  DRR Quantum      : " << quantum_arg.getValue() << " units\n";
  std::cout << "  Output Dir       : " << output_dir_arg.getValue() << "\n";

  Envoy::Event::DRRGoodputExperiment experiment(
      duration_arg.getValue(), interval_arg.getValue(), well_behaved_rate_arg.getValue(),
      noisy_rate_arg.getValue(), num_well_behaved_arg.getValue(), num_noisy_arg.getValue(),
      quantum_arg.getValue(), cpu_budget_arg.getValue(), ramp_noisy_arg.getValue());

  auto legacy_results = experiment.run(false);
  auto drr_results = experiment.run(true);

  std::string output_dir = output_dir_arg.getValue();
  Envoy::TestEnvironment::createPath(output_dir);
  std::string legacy_csv_path = output_dir + "/goodput_legacy.csv";
  std::string drr_csv_path = output_dir + "/goodput_drr.csv";

  Envoy::Event::writeCsv(legacy_csv_path, legacy_results);
  Envoy::Event::writeCsv(drr_csv_path, drr_results);

  std::cout << "\nWrote Legacy FIFO results to : " << legacy_csv_path << "\n";
  std::cout << "Wrote DRR Dispatcher results to: " << drr_csv_path << "\n";

  Envoy::Event::printSummaryTable(legacy_results, drr_results);

  return 0;
}
