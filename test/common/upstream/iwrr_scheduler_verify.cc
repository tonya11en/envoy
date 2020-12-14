#include <algorithm>
#include <memory>
#include <iostream>

#include "common/upstream/edf_scheduler.h"
#include "common/upstream/iwrr_scheduler.h"
#include "common/upstream/wrsq_scheduler.h"

using namespace Envoy::Upstream;

/**
 * Hacking together some quick checks.
 */

size_t syncCheck(RRScheduler<uint32_t>& sched1, RRScheduler<uint32_t>& sched2) {
  size_t collisions = 0;

  std::vector<std::pair<double, std::shared_ptr<uint32_t>>> inputs;
  for (uint32_t i = 1; i < 1000; ++i) {
    inputs.emplace_back(i, std::make_shared<uint32_t>(i));
  }

  std::random_shuffle(inputs.begin(), inputs.end());

  for (const auto& p : inputs) {
    sched1.add(p.first, p.second);
  }

  std::random_shuffle(inputs.begin(), inputs.end());

  for (const auto& p : inputs) {
    sched2.add(p.first, p.second);
  }

  auto fn = [](const auto& obj) { return static_cast<double>(obj); };
  for (int i = 0; i < 10000; ++i) {
    auto r1 = sched1.pickAndAdd(fn);
    auto r2 = sched2.pickAndAdd(fn);
    if (*r1 == *r2) {
      ++collisions;
    }
  }

  return collisions;
}

void selectionCounts(RRScheduler<uint32_t>& sched) {
  std::cout << "diff between observed and expected % (1.0 means 1%)\n";

  // Try 16 objects for sane plots.
  constexpr size_t num_objs = 16;

  constexpr size_t n = 10000;

  std::vector<std::pair<double, std::shared_ptr<uint32_t>>> inputs;
  for (uint32_t i = 1; i <= num_objs; ++i) {
    inputs.emplace_back(static_cast<double>(i), std::make_shared<uint32_t>(i));
  }

  std::random_shuffle(inputs.begin(), inputs.end());
  for (const auto& p : inputs) {
    sched.add(p.first, p.second);
  }


  std::unordered_map<uint32_t, size_t> selection_counts;
  auto fn = [](const auto& obj) { return static_cast<double>(obj); };
  for (size_t i = 0; i < n; ++i) {
    auto p = sched.pickAndAdd(fn);
    selection_counts[*p]++;
  }

  double weight_sum = 0;
  for (const auto& i : inputs) {
    weight_sum += i.first;
  }

  for (const auto& p : selection_counts) {
    const double observed_pct = 100 * static_cast<double>(p.second) / n;
    const double expected_pct = 100 * static_cast<double>(p.first) / weight_sum;
    std::cout << (expected_pct - observed_pct) << std::endl;
  }
}

// -------------------------------------------------------------------------------------------------

void syncCheckWRSQ() {
  WRSQScheduler<uint32_t> wrsq1, wrsq2;
  std::cout << "WRSQ collisions: " << syncCheck(wrsq1, wrsq2) << std::endl;
}

void syncCheckIWRR() {
  IWRRScheduler<uint32_t> iwrr1, iwrr2;
  std::cout << "IWRR collisions: " << syncCheck(iwrr1, iwrr2) << std::endl;
}

void syncCheckEDF() {
  EdfScheduler<uint32_t> edf1, edf2;
  std::cout << "EDF collisions: " << syncCheck(edf1, edf2) << std::endl;
}

void countsIWRR() {
  std::cout << "---\nIWRR selections\n--\n";
  IWRRScheduler<uint32_t> iwrr;
  selectionCounts(iwrr);
}

void countsEDF() {
  std::cout << "---\nEDF selections\n--\n";
  EdfScheduler<uint32_t> edf;
  selectionCounts(edf);
}

void countsWRSQ() {
  std::cout << "---\nWRSQ selections\n--\n";
  WRSQScheduler<uint32_t> wrsq;
  selectionCounts(wrsq);
}

int main() {
  syncCheckEDF();
  syncCheckIWRR();
  syncCheckWRSQ();
  countsEDF();
  countsIWRR();
  countsWRSQ();

  return 0;
}
