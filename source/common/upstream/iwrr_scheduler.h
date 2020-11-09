#include <algorithm>
#include <memory>
#include <iostream>

#include "common/upstream/edf_scheduler.h"

namespace Envoy {
namespace Upstream {
namespace {

/**
 * IWRR scheduler.
 */
template <class C>
class IWRRScheduler : public Scheduler<C> {
 public:
  std::shared_ptr<C> peekAgain(std::function<double(const C&)> ) override {
    return std::shared_ptr<C>();
  }

  std::shared_ptr<C> pickAndAdd(std::function<double(const C&)> ) override {
    if (curr_weights_.empty()) {
      curr_weights_ = weights_;
    }

    auto ret = std::move(curr_weights_.front());
    curr_weights_.pop();
    --ret.second;
    if (ret.second > 0) {
      curr_weights_.emplace(ret);
    }

    return ret.first;
  }

  void add(double weight, std::shared_ptr<C> entry) override {
    curr_weights_.emplace(entry, weight);
    weights_.emplace(entry, weight);
  }

  bool empty() const override {
    return weights_.empty();
  }

 private:
  // TODO do the swap thing
  std::queue<std::pair<std::shared_ptr<C>, double>> curr_weights_;

  std::queue<std::pair<std::shared_ptr<C>, double>> weights_;
};

} // namespace
} // namespace Upstream
} // namespace Envoy


