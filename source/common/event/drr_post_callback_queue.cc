#include "source/common/event/drr_post_callback_queue.h"

#include <algorithm>
#include <limits>

namespace Envoy {
namespace Event {

DRRPostCallbackQueue::DRRPostCallbackQueue(uint32_t default_quantum_units)
    : default_quantum_units_(default_quantum_units) {
  if (default_quantum_units == 0) {
    throw EnvoyException("DRR quantum cannot be zero");
  }
}

void DRRPostCallbackQueue::enqueue(TenantId tenant_id, PostCb cb, uint32_t cost_units) {
  auto it = tenant_queues_.find(tenant_id);
  if (it == tenant_queues_.end()) {
    TenantQueue new_queue;
    new_queue.quantum = default_quantum_units_;
    new_queue.deficit = default_quantum_units_;
    active_tenants_.push_back(tenant_id);
    auto insert_res = tenant_queues_.emplace(tenant_id, std::move(new_queue));
    it = insert_res.first;
  }

  it->second.callbacks.push(TenantPostCallback{std::move(cb), cost_units});
  total_size_++;
}

DRRPostCallbackQueue::PopSliceResult DRRPostCallbackQueue::popSlice(uint32_t max_total_cost_units) {
  PopSliceResult result;
  uint64_t total_processed_cost = 0;
  bool budget_cap_reached = false;

  while (!active_tenants_.empty() && !budget_cap_reached &&
         (total_processed_cost < max_total_cost_units || total_processed_cost == 0)) {
    ++pop_slice_pass_count_for_test_;
    size_t pass_active_count = active_tenants_.size();
    bool any_callback_executed_this_pass = false;

    for (size_t pass_step = 0;
         pass_step < pass_active_count && !active_tenants_.empty() && !budget_cap_reached &&
         (total_processed_cost < max_total_cost_units || total_processed_cost == 0);
         ++pass_step) {
      if (current_tenant_it_ == active_tenants_.end()) {
        current_tenant_it_ = active_tenants_.begin();
      }

      TenantId current_tenant = *current_tenant_it_;
      auto it = tenant_queues_.find(current_tenant);
      if (it == tenant_queues_.end() || it->second.callbacks.empty()) {
        if (it != tenant_queues_.end()) {
          tenant_queues_.erase(it);
        }
        current_tenant_it_ = active_tenants_.erase(current_tenant_it_);
        if (active_tenants_.empty()) {
          current_tenant_it_ = active_tenants_.end();
          break;
        }
        continue;
      }

      TenantQueue& tenant_queue = it->second;

      bool broke_due_to_deficit = false;
      while (!tenant_queue.callbacks.empty() && !budget_cap_reached &&
             (total_processed_cost < max_total_cost_units || total_processed_cost == 0)) {
        const TenantPostCallback& head = tenant_queue.callbacks.front();
        uint64_t cost = std::max(1u, head.estimated_cost_units);
        if (total_processed_cost > 0 && cost > max_total_cost_units) {
          budget_cap_reached = true;
          break;
        }
        if (cost > tenant_queue.deficit) {
          broke_due_to_deficit = true;
          break;
        }

        TenantPostCallback item = std::move(tenant_queue.callbacks.front());
        tenant_queue.callbacks.pop();
        total_size_--;

        tenant_queue.deficit -= cost;
        total_processed_cost += cost;

        result.callbacks.push_back(std::move(item.callback));
        any_callback_executed_this_pass = true;
      }

      if (tenant_queue.callbacks.empty()) {
        tenant_queues_.erase(it);
        current_tenant_it_ = active_tenants_.erase(current_tenant_it_);
        if (active_tenants_.empty()) {
          current_tenant_it_ = active_tenants_.end();
          break;
        }
      } else if (broke_due_to_deficit) {
        tenant_queue.deficit += tenant_queue.quantum;
        ++current_tenant_it_;
      } else {
        // Exited inner loop because max_total_cost_units budget cap was reached.
        // Do NOT advance current_tenant_it_ so the current tenant resumes its remaining
        // deficit in the next slice.
        break;
      }
    }

    if (!any_callback_executed_this_pass) {
      // Fast-forward deficit accumulation for high-cost callbacks so we don't O(N) busy-wait spin.
      uint64_t min_rounds_needed = std::numeric_limits<uint64_t>::max();
      for (const TenantId& tenant_id : active_tenants_) {
        auto it = tenant_queues_.find(tenant_id);
        if (it != tenant_queues_.end() && !it->second.callbacks.empty()) {
          uint64_t cost = std::max(1u, it->second.callbacks.front().estimated_cost_units);
          if (cost <= it->second.deficit) {
            min_rounds_needed = 0;
            break;
          }
          uint64_t needed_deficit = cost - it->second.deficit;
          uint64_t rounds = (needed_deficit + it->second.quantum - 1) / it->second.quantum;
          min_rounds_needed = std::min(min_rounds_needed, rounds);
        }
      }
      if (min_rounds_needed > 1 && min_rounds_needed != std::numeric_limits<uint64_t>::max()) {
        uint64_t skip_rounds = min_rounds_needed - 1;
        for (const TenantId& tenant_id : active_tenants_) {
          auto it = tenant_queues_.find(tenant_id);
          if (it != tenant_queues_.end()) {
            it->second.deficit += skip_rounds * it->second.quantum;
          }
        }
      }
    }
  }

  result.has_more = total_size_ > 0;
  return result;
}

bool DRRPostCallbackQueue::runSlice(uint32_t max_total_cost_units,
                                    const std::function<void()>& watchdog_touch_cb) {
  PopSliceResult slice = popSlice(max_total_cost_units);
  for (auto& cb : slice.callbacks) {
    if (watchdog_touch_cb) {
      watchdog_touch_cb();
    }
    auto current_cb = std::move(cb);
    current_cb();
  }
  return slice.has_more;
}

void DRRPostCallbackQueue::clear() {
  tenant_queues_.clear();
  active_tenants_.clear();
  current_tenant_it_ = active_tenants_.end();
  total_size_ = 0;
}

} // namespace Event
} // namespace Envoy
