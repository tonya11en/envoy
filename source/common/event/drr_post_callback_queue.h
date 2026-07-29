#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <queue>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Event {

using TenantId = uintptr_t;
inline constexpr TenantId DefaultTenantId = 0;

struct TenantPostCallback {
  PostCb callback;
  uint32_t estimated_cost_units{1};
};

/**
 * Implements a Deficit Round-Robin (DRR) post callback queue for Event::Dispatcher.
 * Callbacks are bucketed per tenant ID. During each dispatch pass, active tenants
 * are serviced in round-robin order up to their allocated quantum deficit.
 */
class DRRPostCallbackQueue {
public:
  DRRPostCallbackQueue(uint32_t default_quantum_units = 10);

  DRRPostCallbackQueue(const DRRPostCallbackQueue&) = delete;
  DRRPostCallbackQueue& operator=(const DRRPostCallbackQueue&) = delete;

  DRRPostCallbackQueue(DRRPostCallbackQueue&& other) noexcept
      : default_quantum_units_(other.default_quantum_units_), total_size_(other.total_size_),
        tenant_queues_(std::move(other.tenant_queues_)),
        active_tenants_(std::move(other.active_tenants_)),
        current_tenant_it_(active_tenants_.begin()) {
    other.total_size_ = 0;
    other.current_tenant_it_ = other.active_tenants_.end();
  }

  DRRPostCallbackQueue& operator=(DRRPostCallbackQueue&& other) noexcept {
    if (this != &other) {
      total_size_ = other.total_size_;
      tenant_queues_ = std::move(other.tenant_queues_);
      active_tenants_ = std::move(other.active_tenants_);
      current_tenant_it_ = active_tenants_.begin();
      other.total_size_ = 0;
      other.current_tenant_it_ = other.active_tenants_.end();
    }
    return *this;
  }

  /**
   * Enqueue a post callback associated with a tenant ID.
   * @param tenant_id string identifier for the tenant.
   * @param cb callback function to post.
   * @param cost_units estimated cost of this callback (default 1).
   */
  void enqueue(TenantId tenant_id, PostCb cb, uint32_t cost_units = 1);

  struct PopSliceResult {
    std::vector<PostCb> callbacks;
    bool has_more{false};
  };

  /**
   * Dequeue a slice of callbacks using DRR round-robin among active tenants.
   * @param max_total_cost_units maximum aggregate cost allowed in this execution slice.
   * @return PopSliceResult containing dequeued callbacks and whether more remain.
   */
  PopSliceResult popSlice(uint32_t max_total_cost_units);

  /**
   * Execute a slice of callbacks using DRR round-robin among active tenants.
   * @param max_total_cost_units maximum aggregate cost allowed in this execution slice.
   * @param watchdog_touch_cb callback to touch the watchdog between callback executions.
   * @return true if unexecuted callbacks remain across any tenant queue.
   */
  bool runSlice(uint32_t max_total_cost_units, const std::function<void()>& watchdog_touch_cb);

  /**
   * @return true if all tenant queues are empty.
   */
  bool empty() const { return total_size_ == 0; }

  /**
   * @return size_t total queued callback count across all tenants.
   */
  size_t size() const { return total_size_; }

  /**
   * Clears all queued callbacks.
   */
  void clear();

  /**
   * @return size_t count of tenant queue entries in map.
   */
  size_t numTenantQueues() const { return tenant_queues_.size(); }

  /**
   * @return uint32_t default quantum units per tenant turn.
   */
  uint32_t defaultQuantum() const { return default_quantum_units_; }

  /**
   * @return bool whether the given tenant has a queue in this DRR queue.
   */
  bool hasTenant(TenantId tenant_id) const {
    return tenant_queues_.find(tenant_id) != tenant_queues_.end();
  }

  // Test helpers
  void addEmptyTenantForTest(TenantId tenant_id) {
    auto it = tenant_queues_.find(tenant_id);
    if (it == tenant_queues_.end()) {
      TenantQueue new_queue;
      new_queue.quantum = default_quantum_units_;
      new_queue.deficit = default_quantum_units_;
      active_tenants_.push_back(tenant_id);
      tenant_queues_.emplace(tenant_id, std::move(new_queue));
    }
  }

  uint64_t popSlicePassCountForTest() const { return pop_slice_pass_count_for_test_; }
  void resetPopSlicePassCountForTest() { pop_slice_pass_count_for_test_ = 0; }

private:
  struct TenantQueue {
    std::queue<TenantPostCallback> callbacks;
    uint64_t deficit{0};
    uint32_t quantum{10};
  };

  const uint32_t default_quantum_units_;
  size_t total_size_{0};
  uint64_t pop_slice_pass_count_for_test_{0};
  absl::flat_hash_map<TenantId, TenantQueue> tenant_queues_;
  std::list<TenantId> active_tenants_;
  std::list<TenantId>::iterator current_tenant_it_{active_tenants_.end()};
};

} // namespace Event
} // namespace Envoy
