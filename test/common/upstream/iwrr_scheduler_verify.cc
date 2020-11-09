#include <algorithm>
#include <memory>
#include <iostream>

#include "common/upstream/edf_scheduler.h"
#include "common/upstream/iwrr_scheduler.h"

using namespace Envoy::Upstream;

void syncCheckIWRR() {
  IWRRScheduler<uint32_t> edf1, edf2;

  size_t collisions = 0;

  std::vector<std::pair<double, std::shared_ptr<uint32_t>>> inputs;
  for (uint32_t i = 1; i < 1000; ++i) {
    inputs.emplace_back(i, std::make_shared<uint32_t>(i));
  }

  std::random_shuffle(inputs.begin(), inputs.end());
  for (const auto& p : inputs) {
    edf1.add(p.first, p.second);
  }

  std::random_shuffle(inputs.begin(), inputs.end());
  for (const auto& p : inputs) {
    edf2.add(p.first, p.second);
  }

  for (int i = 0; i < 10000; ++i) {
    auto r1 = edf1.pickAndAdd([](const auto& obj) {
      return static_cast<double>(obj);
    });
    auto r2 = edf2.pickAndAdd([](const auto& obj) {
      return static_cast<double>(obj);
    });

    if (*r1 == *r2) {
      ++collisions;
    }
  }

  std::cout << "IWRR collisions: " << collisions << std::endl;
}

void syncCheckEDF() {
  EdfScheduler<uint32_t> edf1, edf2;

  size_t collisions = 0;

  std::vector<std::pair<double, std::shared_ptr<uint32_t>>> inputs;
  for (uint32_t i = 1; i < 1000; ++i) {
    inputs.emplace_back(i, std::make_shared<uint32_t>(i));
  }

  std::random_shuffle(inputs.begin(), inputs.end());
  for (const auto& p : inputs) {
    edf1.add(p.first, p.second);
  }

  std::random_shuffle(inputs.begin(), inputs.end());
  for (const auto& p : inputs) {
    edf2.add(p.first, p.second);
  }

  for (int i = 0; i < 10000; ++i) {
    auto r1 = edf1.pickAndAdd([](const auto& obj) {
      return static_cast<double>(obj);
    });
    auto r2 = edf2.pickAndAdd([](const auto& obj) {
      return static_cast<double>(obj);
    });

    if (*r1 == *r2) {
      ++collisions;
    }
  }

  std::cout << "EDF collisions: " << collisions << std::endl;
}

int main() {
  EdfScheduler<uint32_t> edf;

  syncCheckEDF();
  syncCheckIWRR();

  return 0;
}
