#include "source/common/event/drr_post_callback_queue.h"

#include <algorithm>

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

  while (!active_tenants_.empty() &&
         (total_processed_cost < max_total_cost_units || total_processed_cost == 0)) {
    size_t pass_active_count = active_tenants_.size();
    bool any_callback_executed_this_pass = false;

    for (size_t pass_step = 0;
         pass_step < pass_active_count && !active_tenants_.empty() &&
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
        if (pass_active_count > 0) {
          --pass_active_count;
          if (pass_step > 0) {
            --pass_step;
          }
        }
        if (active_tenants_.empty()) {
          current_tenant_it_ = active_tenants_.end();
          break;
        }
        continue;
      }

      TenantQueue& tenant_queue = it->second;

      bool broke_due_to_deficit = false;
      while (!tenant_queue.callbacks.empty() &&
             (total_processed_cost < max_total_cost_units || total_processed_cost == 0)) {
        const TenantPostCallback& head = tenant_queue.callbacks.front();
        uint64_t cost = std::max(1u, head.estimated_cost_units);
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
        // Advance current_tenant_it_ so the next slice starts at the next tenant.
        ++current_tenant_it_;
        break;
      }
    }

    if (!any_callback_executed_this_pass && total_processed_cost > 0) {
      break;
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
