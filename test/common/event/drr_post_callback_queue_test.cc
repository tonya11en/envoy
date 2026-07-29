#include "source/common/event/drr_post_callback_queue.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Event {
namespace {

constexpr TenantId TenantA = 1;
constexpr TenantId TenantB = 2;
constexpr TenantId TenantC = 3;
constexpr TenantId TenantD = 4;
constexpr TenantId Tenant1 = 10;
constexpr TenantId Tenant2 = 20;

// Verifies baseline FIFO execution order for callbacks belonging to a single tenant.
TEST(DRRPostCallbackQueueTest, SingleTenantFifoBaseline) {
  DRRPostCallbackQueue queue(10);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);

  std::vector<int> execution_order;
  queue.enqueue(TenantA, [&]() { execution_order.push_back(1); });
  queue.enqueue(TenantA, [&]() { execution_order.push_back(2); });
  queue.enqueue(TenantA, [&]() { execution_order.push_back(3); });

  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.size(), 3);

  bool remaining = queue.runSlice(100, nullptr);
  EXPECT_FALSE(remaining);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(execution_order, (std::vector<int>{1, 2, 3}));
}

// Verifies fair round-robin scheduling across multiple tenants based on their quantum limits.
TEST(DRRPostCallbackQueueTest, MultiTenantFairRoundRobin) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/2);

  std::vector<std::string> execution_order;

  // Tenant A enqueues 5 callbacks (cost 1 each)
  for (int i = 1; i <= 5; i++) {
    queue.enqueue(TenantA,
                  [i, &execution_order]() { execution_order.push_back("A" + std::to_string(i)); });
  }

  // Tenant B enqueues 2 callbacks (cost 1 each)
  for (int i = 1; i <= 2; i++) {
    queue.enqueue(TenantB,
                  [i, &execution_order]() { execution_order.push_back("B" + std::to_string(i)); });
  }

  EXPECT_EQ(queue.size(), 7);

  // First slice allows 4 callbacks (Quantum is 2 per tenant turn)
  bool remaining = queue.runSlice(/*max_total_cost_units=*/4, nullptr);
  EXPECT_TRUE(remaining);

  // Tenant A should get 2, Tenant B should get 2
  EXPECT_EQ(execution_order, (std::vector<std::string>{"A1", "A2", "B1", "B2"}));

  // Run next slice to finish Tenant A's remaining callbacks
  remaining = queue.runSlice(/*max_total_cost_units=*/10, nullptr);
  EXPECT_FALSE(remaining);
  EXPECT_TRUE(queue.empty());

  EXPECT_EQ(execution_order, (std::vector<std::string>{"A1", "A2", "B1", "B2", "A3", "A4", "A5"}));
}

// Verifies deficit rollover across execution slices and aggregate cost tracking.
TEST(DRRPostCallbackQueueTest, DeficitRolloverAndCostTracking) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/3);

  std::vector<std::string> execution_order;

  // High cost callback (cost 3)
  queue.enqueue(TenantA, [&]() { execution_order.push_back("A1"); }, /*cost_units=*/3);
  queue.enqueue(TenantA, [&]() { execution_order.push_back("A2"); }, /*cost_units=*/3);

  queue.enqueue(TenantB, [&]() { execution_order.push_back("B1"); }, /*cost_units=*/1);

  // Slice budget is 4 units (A1=3 + B1=1 = 4)
  bool remaining = queue.runSlice(/*max_total_cost_units=*/4, nullptr);
  EXPECT_TRUE(remaining);

  EXPECT_EQ(execution_order, (std::vector<std::string>{"A1", "B1"}));

  // Second slice finishes A2
  remaining = queue.runSlice(/*max_total_cost_units=*/10, nullptr);
  EXPECT_FALSE(remaining);
  EXPECT_EQ(execution_order, (std::vector<std::string>{"A1", "B1", "A2"}));
}

// Verifies that clear() removes all queued callbacks across all tenants.
TEST(DRRPostCallbackQueueTest, ClearQueue) {
  DRRPostCallbackQueue queue(10);
  queue.enqueue(TenantA, []() {});
  queue.enqueue(TenantB, []() {});
  EXPECT_EQ(queue.size(), 2);

  queue.clear();
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
}

// Verifies that callbacks are destroyed immediately after execution in FIFO order.
TEST(DRRPostCallbackQueueTest, CallbackDestructionOrder) {
  DRRPostCallbackQueue queue(10);
  bool cb1_destroyed = false;
  bool cb2_ran_after_cb1_destroyed = false;

  struct DestructNotifier {
    std::function<void()> on_destroy;
    ~DestructNotifier() {
      if (on_destroy) {
        on_destroy();
      }
    }
  };

  auto notifier = std::make_shared<DestructNotifier>();
  notifier->on_destroy = [&]() { cb1_destroyed = true; };

  queue.enqueue(TenantA, [notifier]() {
    // Callback 1 holds notifier
  });

  queue.enqueue(TenantA, [&]() {
    // Callback 2 runs after Callback 1
    if (cb1_destroyed) {
      cb2_ran_after_cb1_destroyed = true;
    }
  });

  notifier.reset();
  queue.runSlice(100, nullptr);
  EXPECT_TRUE(cb2_ran_after_cb1_destroyed);
}

// Verifies that callbacks costing more than quantum require multiple deficit rounds before
// executing.
TEST(DRRPostCallbackQueueTest, HighCostCallbackDeficitRequirement) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  // Tenant A callback with cost 15 (greater than quantum 10)
  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, /*cost_units=*/15);

  // Tenant B callback with cost 5 (less than quantum 10)
  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, /*cost_units=*/5);

  // First slice with cost budget 4:
  // Tenant A: quantum = 10, cost = 15. Since 15 > 10, A1 MUST NOT run in round 1.
  // Tenant B: quantum = 10, cost = 5. B1 runs (cost 5 >= budget 4, loop terminates slice 1).
  bool remaining = queue.runSlice(/*max_total_cost_units=*/4, nullptr);
  EXPECT_TRUE(remaining);
  EXPECT_EQ(order, (std::vector<std::string>{"B1"}));

  // Second slice: Tenant A gets another 10 quantum (deficit becomes 20 >= 15). A1 runs.
  remaining = queue.runSlice(/*max_total_cost_units=*/20, nullptr);
  EXPECT_FALSE(remaining);
  EXPECT_EQ(order, (std::vector<std::string>{"B1", "A1"}));
}

// Verifies that empty tenant queue metadata structures are purged after all callbacks execute.
TEST(DRRPostCallbackQueueTest, CleanupEmptyTenantQueues) {
  DRRPostCallbackQueue queue(10);
  queue.enqueue(Tenant1, []() {});
  queue.enqueue(Tenant2, []() {});
  EXPECT_EQ(queue.numTenantQueues(), 2);

  queue.runSlice(100, nullptr);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.numTenantQueues(), 0);
}

// Verifies that removing a completed tenant queue from active_tenants_ does not skip existing
// waiting tenants.
TEST(DRRPostCallbackQueueTest, TenantRemovalDoesNotSkipNextTenant) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/2);
  std::vector<std::string> order;

  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, 1);
  queue.enqueue(TenantA, [&]() { order.push_back("A2"); }, 1);

  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, 1);
  queue.enqueue(TenantB, [&]() { order.push_back("B2"); }, 1);
  queue.enqueue(TenantB, [&]() { order.push_back("B3"); }, 1);
  queue.enqueue(TenantB, [&]() { order.push_back("B4"); }, 1);

  bool remaining = queue.runSlice(/*max_total_cost_units=*/3, nullptr);
  EXPECT_TRUE(remaining);
  EXPECT_EQ(order, (std::vector<std::string>{"A1", "A2", "B1"}));

  queue.enqueue(TenantC, [&]() { order.push_back("C1"); }, 1);
  queue.enqueue(TenantC, [&]() { order.push_back("C2"); }, 1);

  remaining = queue.runSlice(/*max_total_cost_units=*/10, nullptr);
  EXPECT_FALSE(remaining);
  EXPECT_EQ(order, (std::vector<std::string>{"A1", "A2", "B1", "B2", "C1", "C2", "B3", "B4"}));
}

// Verifies deficit carryover across slices when rate-limited by the global slice budget.
TEST(DRRPostCallbackQueueTest, DeficitCarryoverAcrossSlices) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  // Tenant A enqueues 6 callbacks of cost 5 each
  for (int i = 1; i <= 6; i++) {
    queue.enqueue(TenantA, [&, i]() { order.push_back("A" + std::to_string(i)); }, 5);
  }

  // 3 small slices of max budget 5
  queue.runSlice(/*max_total_cost_units=*/5, nullptr); // A1 (cost 5)
  queue.runSlice(/*max_total_cost_units=*/5, nullptr); // A2 (cost 5)
  queue.runSlice(/*max_total_cost_units=*/5, nullptr); // A3 (cost 5)

  // Enqueue Tenant B with 4 callbacks of cost 5
  for (int i = 1; i <= 4; i++) {
    queue.enqueue(TenantB, [&, i]() { order.push_back("B" + std::to_string(i)); }, 5);
  }

  queue.runSlice(/*max_total_cost_units=*/100, nullptr);
  EXPECT_EQ(order,
            (std::vector<std::string>{"A1", "A2", "A3", "A4", "B1", "B2", "A5", "A6", "B3", "B4"}));
}

// Verifies that a high-cost callback (> 2 * quantum) eventually executes after accumulating
// deficit across multiple turns without being starved or truncated.
TEST(DRRPostCallbackQueueTest, HighCostCallbackDeficitAccumulation) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  // Tenant A enqueues a callback with cost 35 (quantum is 10)
  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, /*cost_units=*/35);

  // Tenant B enqueues 3 small callbacks (cost 5 each)
  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, /*cost_units=*/5);
  queue.enqueue(TenantB, [&]() { order.push_back("B2"); }, /*cost_units=*/5);
  queue.enqueue(TenantB, [&]() { order.push_back("B3"); }, /*cost_units=*/5);

  bool remaining = queue.runSlice(/*max_total_cost_units=*/4, nullptr);
  EXPECT_TRUE(remaining);
  EXPECT_EQ(order, (std::vector<std::string>{"B1"}));

  remaining = queue.runSlice(/*max_total_cost_units=*/4, nullptr);
  EXPECT_TRUE(remaining);
  EXPECT_EQ(order, (std::vector<std::string>{"B1", "B2"}));

  remaining = queue.runSlice(/*max_total_cost_units=*/4, nullptr);
  EXPECT_TRUE(remaining);
  EXPECT_EQ(order, (std::vector<std::string>{"B1", "B2", "B3"}));

  remaining = queue.runSlice(/*max_total_cost_units=*/100, nullptr);
  EXPECT_FALSE(remaining);
  EXPECT_EQ(order, (std::vector<std::string>{"B1", "B2", "B3", "A1"}));
}

// Verifies that a single tenant with a high-cost callback (greater than quantum) executes
// without aggressively yielding to the event loop.
TEST(DRRPostCallbackQueueTest, SingleHighCostCallbackYieldsWithoutSpinning) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  // Single tenant with cost 100 (quantum is 10)
  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, /*cost_units=*/100);

  // Slice budget 50. In pass 1, deficit becomes 10 < 100. It acquires deficit until executable.
  bool remaining = queue.runSlice(/*max_total_cost_units=*/50, nullptr);
  EXPECT_FALSE(remaining);
  EXPECT_EQ(order, (std::vector<std::string>{"A1"}));
}

// Verifies that a high-cost callback (> quantum) accumulates quantum across rounds until cost is
// met.
TEST(DRRPostCallbackQueueTest, HighCostCallbackExecutesWithinSliceBudget) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  // Single tenant with callback cost 30 (quantum is 10)
  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, /*cost_units=*/30);

  bool remaining = queue.runSlice(/*max_total_cost_units=*/50, nullptr);
  EXPECT_FALSE(remaining);
  EXPECT_EQ(order, (std::vector<std::string>{"A1"}));
}

// Verifies that callbacks costing more than max_total_cost_units accumulate deficit and execute
// without deadlocking.
TEST(DRRPostCallbackQueueTest, HighCostCallbackExceedingSliceBudgetExecutes) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  // Callback cost 100 > max_total_cost_units 50
  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, /*cost_units=*/100);

  // Calling runSlice multiple times should accumulate deficit beyond 50 until 100 is reached and A1
  // executes.
  for (int i = 0; i < 10; ++i) {
    queue.runSlice(/*max_total_cost_units=*/50, nullptr);
  }
  EXPECT_EQ(order, (std::vector<std::string>{"A1"}));
}

// Verifies that when a tenant queue empties mid-slice, remaining active tenants are not granted
// extra round-robin turns within the same pass.
TEST(DRRPostCallbackQueueTest, QueueDepletionDoesNotGrantExtraTurnToRemainingTenants) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/2);
  std::vector<std::string> order;

  // Tenant A: 2 callbacks (cost 1 each)
  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, 1);
  queue.enqueue(TenantA, [&]() { order.push_back("A2"); }, 1);

  // Tenant B: 1 callback (cost 1) -> empties out in pass 1
  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, 1);

  // Tenant C: 2 callbacks (cost 1 each)
  queue.enqueue(TenantC, [&]() { order.push_back("C1"); }, 1);
  queue.enqueue(TenantC, [&]() { order.push_back("C2"); }, 1);

  // Slice budget 3:
  // Round 1: Tenant A runs A1, A2 (cost 2). Tenant B runs B1 (cost 1, B queue empties and is
  // erased). Total cost processed = 3.
  bool remaining = queue.runSlice(/*max_total_cost_units=*/3, nullptr);
  EXPECT_TRUE(remaining);
  EXPECT_EQ(order, (std::vector<std::string>{"A1", "A2", "B1"}));

  // Second slice with budget 10:
  // Round 1 must finish with Tenant C running C1, C2 BEFORE any new round starts for Tenant A.
  order.clear();
  queue.runSlice(/*max_total_cost_units=*/10, nullptr);
  EXPECT_EQ(order, (std::vector<std::string>{"C1", "C2"}));
}

// Verifies Bug 3: Round-robin fairness is strictly maintained when a middle tenant queue empties.
TEST(DRRPostCallbackQueueTest, FourTenantsQueueDepletionStrictRoundRobin) {

  DRRPostCallbackQueue queue(/*default_quantum_units=*/1);
  std::vector<std::string> order;

  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, 1);
  queue.enqueue(TenantA, [&]() { order.push_back("A2"); }, 1);

  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, 1);

  queue.enqueue(TenantC, [&]() { order.push_back("C1"); }, 1);
  queue.enqueue(TenantC, [&]() { order.push_back("C2"); }, 1);

  queue.enqueue(TenantD, [&]() { order.push_back("D1"); }, 1);

  // In round-robin pass 1: A1, B1 (empties), C1, D1 must execute in order.
  // Tenant A must NOT get A2 in pass 1 before D1 has executed!
  auto slice = queue.popSlice(/*max_total_cost_units=*/4);
  for (auto& cb : slice.callbacks) {
    cb();
  }

  EXPECT_EQ(order, (std::vector<std::string>{"A1", "B1", "C1", "D1"}));
}

// Verifies that quantum cannot be zero (prevents infinite loop).
TEST(DRRPostCallbackQueueTest, ZeroQuantumThrowsException) {
  EXPECT_THROW(DRRPostCallbackQueue queue(0), EnvoyException);
}

// Verifies step accounting when an empty tenant queue entry is erased at step start.
TEST(DRRPostCallbackQueueTest, ErasingEmptyTenantAtStepStartPreservesRoundRobinPass) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/1);
  std::vector<std::string> order;

  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, 1);
  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, 1);
  queue.enqueue(TenantC, [&]() { order.push_back("C1"); }, 1);

  // Run first slice to process A1 and B1 (A and B queues empty, but map entries might linger)
  queue.runSlice(2, nullptr);
  EXPECT_EQ(order, (std::vector<std::string>{"A1", "B1"}));

  // Enqueue new items for Tenant D
  queue.enqueue(TenantD, [&]() { order.push_back("D1"); }, 1);

  // Run next slice: Tenant C and Tenant D must BOTH be evaluated in the pass.
  order.clear();
  queue.runSlice(10, nullptr);
  EXPECT_EQ(order, (std::vector<std::string>{"C1", "D1"}));
}

// Verifies that the hybrid slicing strategy clamps the lower slice budget floor to 50 units
// when active tenant queues have low total quantum.
TEST(DRRPostCallbackQueueTest, HybridSlicingClampsToMinimumFloor) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);

  // Enqueue 60 callbacks of cost 1 under 1 tenant
  for (int i = 0; i < 60; i++) {
    queue.enqueue(TenantA, []() {});
  }

  // Calculate dynamic max slice cost per hybrid strategy formula:
  // std::min(500u, std::max(50u, numTenantQueues() * defaultQuantum()))
  uint32_t dynamic_cost =
      std::max(50u, static_cast<uint32_t>(queue.numTenantQueues() * queue.defaultQuantum()));
  uint32_t max_slice_cost = std::min(500u, dynamic_cost);

  // 1 tenant * quantum 10 = 10 units < 50 minimum floor. Clamped to 50.
  EXPECT_EQ(max_slice_cost, 50);

  auto slice = queue.popSlice(max_slice_cost);
  EXPECT_EQ(slice.callbacks.size(), 50);
  EXPECT_TRUE(slice.has_more);

  // Second slice processes remaining 10 callbacks.
  slice = queue.popSlice(max_slice_cost);
  EXPECT_EQ(slice.callbacks.size(), 10);
  EXPECT_FALSE(slice.has_more);
}

// Verifies that the hybrid slicing strategy dynamically scales up the slice budget
// proportionally with the number of active tenant queues.
TEST(DRRPostCallbackQueueTest, HybridSlicingScalesWithActiveTenantCount) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);

  // Create 10 distinct tenants (TenantId 1..10), each enqueuing 15 callbacks (total 150)
  for (TenantId t = 1; t <= 10; t++) {
    for (int j = 0; j < 15; j++) {
      queue.enqueue(t, []() {});
    }
  }

  // Calculate dynamic max slice cost per hybrid strategy formula:
  uint32_t dynamic_cost =
      std::max(50u, static_cast<uint32_t>(queue.numTenantQueues() * queue.defaultQuantum()));
  uint32_t max_slice_cost = std::min(500u, dynamic_cost);

  // 10 tenants * quantum 10 = 100 units.
  EXPECT_EQ(max_slice_cost, 100);

  auto slice = queue.popSlice(max_slice_cost);
  EXPECT_EQ(slice.callbacks.size(), 100);
  EXPECT_TRUE(slice.has_more);

  // Second slice processes remaining 50 callbacks.
  slice = queue.popSlice(max_slice_cost);
  EXPECT_EQ(slice.callbacks.size(), 50);
  EXPECT_FALSE(slice.has_more);
}

// Verifies that the hybrid slicing strategy caps the maximum slice budget at 500 units
// under high tenant queue contention to protect event loop tail latency.
TEST(DRRPostCallbackQueueTest, HybridSlicingCapsAtMaximumCeiling) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);

  // Create 60 distinct tenants (TenantId 1..60), each enqueuing 10 callbacks (total 600)
  for (TenantId t = 1; t <= 60; t++) {
    for (int j = 0; j < 10; j++) {
      queue.enqueue(t, []() {});
    }
  }

  // Calculate dynamic max slice cost per hybrid strategy formula:
  uint32_t dynamic_cost =
      std::max(50u, static_cast<uint32_t>(queue.numTenantQueues() * queue.defaultQuantum()));
  uint32_t max_slice_cost = std::min(500u, dynamic_cost);

  // 60 tenants * quantum 10 = 600 units > 500 maximum ceiling. Clamped to 500.
  EXPECT_EQ(max_slice_cost, 500);

  auto slice = queue.popSlice(max_slice_cost);
  EXPECT_EQ(slice.callbacks.size(), 500);
  EXPECT_TRUE(slice.has_more);

  // Second slice processes remaining 100 callbacks.
  slice = queue.popSlice(max_slice_cost);
  EXPECT_EQ(slice.callbacks.size(), 100);
  EXPECT_FALSE(slice.has_more);
}

} // namespace
} // namespace Event
} // namespace Envoy

namespace Envoy {
namespace Event {
namespace {

// Test 1 (Bug 2): High-cost callback causing spin loop / max_total_cost_units violation.
TEST(DRRPostCallbackQueueTest, HighCostCallbackDoesNotSpinLoopOrViolateMaxTotalCost) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  // Tenant A: 1 low-cost callback (cost 1)
  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, /*cost_units=*/1);
  // Tenant B: 1 high-cost callback (cost 10,000 units, quantum=10)
  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, /*cost_units=*/10000);

  // In a single popSlice call with max_total_cost_units = 50:
  // Tenant A1 runs (total_processed_cost = 1).
  // Next pass: Tenant B cost 10,000 > deficit 10. No callback runs for Tenant B.
  // Since total_processed_cost = 1 > 0 and no callback executed in this pass, popSlice yields!
  // It does NOT spin 1,000 times in a tight loop to run B1!
  auto slice = queue.popSlice(/*max_total_cost_units=*/50);
  EXPECT_EQ(slice.callbacks.size(), 1);
  EXPECT_TRUE(slice.has_more);
}

// Test 2 (Bug 3): Move constructor/assignment resetting iterator.
TEST(DRRPostCallbackQueueTest, MoveContainerDoesNotInvalidateIterator) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  queue.enqueue(TenantA, []() {});
  queue.enqueue(TenantB, []() {});

  // Pop part of a slice to make current_tenant_it_ point to something in active_tenants_
  auto slice1 = queue.popSlice(/*max_total_cost_units=*/10);

  // Move construct
  DRRPostCallbackQueue moved_queue(std::move(queue));

  // Accessing moved_queue should not segfault or dereference invalid iterator from old queue
  EXPECT_NO_FATAL_FAILURE({ auto slice2 = moved_queue.popSlice(/*max_total_cost_units=*/10); });
}

// Test 3 (Bug 4): Round-robin state bias across execution slices.
TEST(DRRPostCallbackQueueTest, SliceCapAdvancesIteratorToNextTenant) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, 10);
  queue.enqueue(TenantA, [&]() { order.push_back("A2"); }, 10);
  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, 10);
  queue.enqueue(TenantB, [&]() { order.push_back("B2"); }, 10);

  // Slice 1 budget = 10 units. Tenant A runs A1 (cost 10). Hit max_total_cost_units cap!
  auto slice1 = queue.popSlice(/*max_total_cost_units=*/10);
  for (auto& cb : slice1.callbacks) {
    cb();
  }
  EXPECT_EQ(order, (std::vector<std::string>{"A1"}));

  // Slice 2 budget = 10 units. Next slice MUST advance to Tenant B (runs B1) rather than repeating
  // Tenant A (A2).
  auto slice2 = queue.popSlice(/*max_total_cost_units=*/10);
  for (auto& cb : slice2.callbacks) {
    cb();
  }
  EXPECT_EQ(order, (std::vector<std::string>{"A1", "B1"}));
}

// Verifies that when a tenant has remaining deficit after a slice ends due to budget cap,
// it resumes its turn in the next slice.
TEST(DRRPostCallbackQueueTest, InterSliceRoundRobinFairnessSmallCost) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, 2);
  queue.enqueue(TenantA, [&]() { order.push_back("A2"); }, 2);
  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, 2);
  queue.enqueue(TenantB, [&]() { order.push_back("B2"); }, 2);

  // Slice 1: max_total_cost_units = 2. Tenant A runs A1 (cost 2). Slice budget ends.
  auto slice1 = queue.popSlice(2);
  for (auto& cb : slice1.callbacks) {
    cb();
  }
  EXPECT_EQ(order, (std::vector<std::string>{"A1"}));

  // Slice 2: max_total_cost_units = 2. Tenant A has deficit 8 remaining, so it runs A2.
  auto slice2 = queue.popSlice(2);
  for (auto& cb : slice2.callbacks) {
    cb();
  }
  EXPECT_EQ(order, (std::vector<std::string>{"A1", "A2"}));
}

TEST(DRRPostCallbackQueueTest, MoveAssignmentResetsOtherIterator) {
  DRRPostCallbackQueue queue1(10);
  queue1.enqueue(TenantA, []() {});
  queue1.enqueue(TenantB, []() {});

  // Advance iterator in queue1
  queue1.popSlice(1);

  DRRPostCallbackQueue queue2(10);
  queue2 = std::move(queue1);

  // queue1 should now have active_tenants_ empty and current_tenant_it_ pointing to end()
  EXPECT_TRUE(queue1.empty());
  // Adding new item to queue1 must not crash
  EXPECT_NO_FATAL_FAILURE({
    queue1.enqueue(TenantC, []() {});
    queue1.popSlice(10);
  });
}

// Verifies that when a tenant is interrupted mid-round by the slice budget cap while it
// still has remaining positive deficit, it resumes in the next slice rather than forfeiting its
// turn.
TEST(DRRPostCallbackQueueTest, TenantResumesRemainingDeficitWhenInterruptedBySliceBudget) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, 3);
  queue.enqueue(TenantA, [&]() { order.push_back("A2"); }, 3);
  queue.enqueue(TenantA, [&]() { order.push_back("A3"); }, 3);

  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, 3);
  queue.enqueue(TenantB, [&]() { order.push_back("B2"); }, 3);

  // Slice 1: max_total_cost_units = 4. Tenant A runs A1 (cost 3) and A2 (cost 3).
  // Total cost = 6 >= 4. Tenant A still has deficit = 4 remaining in this round.
  auto slice1 = queue.popSlice(4);
  for (auto& cb : slice1.callbacks) {
    cb();
  }
  EXPECT_EQ(order, (std::vector<std::string>{"A1", "A2"}));

  // Slice 2: Tenant A should resume its turn with its remaining deficit 4 to run A3 (cost 3).
  auto slice2 = queue.popSlice(3);
  for (auto& cb : slice2.callbacks) {
    cb();
  }
  EXPECT_EQ(order, (std::vector<std::string>{"A1", "A2", "A3"}));
}

// Verifies that a callback with cost much greater than quantum accumulates deficit in O(1) time
// without spinning in popSlice for millions of loop iterations.
TEST(DRRPostCallbackQueueTest, HighCostCallbackDeficitAccumulationFastForward) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, /*cost_units=*/1000000);
  queue.resetPopSlicePassCountForTest();

  auto slice = queue.popSlice(100);
  for (auto& cb : slice.callbacks) {
    cb();
  }
  EXPECT_LE(queue.popSlicePassCountForTest(), 3);
  EXPECT_EQ(order, (std::vector<std::string>{"A1"}));
}

// Verifies regression: fast-forwarding deficit accumulation skips the exact number of needed
// rounds (min_rounds_needed) without off-by-one errors or disabling fast-forwarding when
// min_rounds_needed == 1.
TEST(DRRPostCallbackQueueTest, FastForwardDeficitAccumulationExactPasses) {
  {
    DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
    std::vector<std::string> order;
    queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, /*cost_units=*/1000000);
    queue.resetPopSlicePassCountForTest();

    auto slice = queue.popSlice(100);
    for (auto& cb : slice.callbacks) {
      cb();
    }
    // Exactly 2 passes: Pass 1 detects deficit deficit and fast-forwards; Pass 2 executes callback.
    EXPECT_EQ(queue.popSlicePassCountForTest(), 2);
    EXPECT_EQ(order, (std::vector<std::string>{"A1"}));
  }
  {
    DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
    std::vector<std::string> order;
    queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, /*cost_units=*/25);
    queue.resetPopSlicePassCountForTest();

    auto slice = queue.popSlice(100);
    for (auto& cb : slice.callbacks) {
      cb();
    }
    // With min_rounds_needed == 1 (cost 25, deficit after Pass 1 is 20), fast-forwarding must skip
    // 1 round so the callback executes on Pass 2.
    EXPECT_EQ(queue.popSlicePassCountForTest(), 2);
    EXPECT_EQ(order, (std::vector<std::string>{"A1"}));
  }
}

// Verifies that an empty tenant queue at the start of a round-robin pass does not skip the last
// tenant.
TEST(DRRPostCallbackQueueTest, EmptyTenantCleanupAtStartDoesNotSkipNextTenants) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  queue.addEmptyTenantForTest(TenantA);
  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, 1);
  queue.enqueue(TenantC, [&]() { order.push_back("C1"); }, 1);

  queue.resetPopSlicePassCountForTest();
  auto slice = queue.popSlice(10);
  for (auto& cb : slice.callbacks) {
    cb();
  }
  EXPECT_EQ(queue.popSlicePassCountForTest(), 1);
  EXPECT_EQ(order, (std::vector<std::string>{"B1", "C1"}));
}

// Verifies Bug 1: Premature slice termination on passes where no callback can execute immediately
// when total_processed_cost > 0.
TEST(DRRPostCallbackQueueTest, MixedCostCallbacksExecuteInSingleSliceWhenWithinBudget) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, 10);
  queue.enqueue(TenantA, [&]() { order.push_back("A2"); }, 100);
  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, 100);

  // max_total_cost_units = 500. Should execute all 3 callbacks within a single slice.
  auto slice = queue.popSlice(500);
  for (auto& cb : slice.callbacks) {
    cb();
  }
  EXPECT_EQ(order, (std::vector<std::string>{"A1", "B1", "A2"}));
  EXPECT_FALSE(slice.has_more);
}

// Verifies Bug 2: Fast-forward deficit accumulation does not over-credit ready tenants.
TEST(DRRPostCallbackQueueTest, FastForwardDoesNotOverCreditReadyTenants) {
  DRRPostCallbackQueue queue(/*default_quantum_units=*/10);
  std::vector<std::string> order;

  // Tenant A starts with deficit 10. A1 cost = 20, A2 cost = 100.
  queue.enqueue(TenantA, [&]() { order.push_back("A1"); }, 20);
  queue.enqueue(TenantA, [&]() { order.push_back("A2"); }, 100);

  // Tenant B starts with deficit 10. B1 cost = 1000.
  queue.enqueue(TenantB, [&]() { order.push_back("B1"); }, 1000);

  // popSlice(30):
  // Pass 1: neither A1 (cost 20 > def 10) nor B1 (cost 1000 > def 10) can run.
  // Both break and get +10 deficit (A deficit=20, B deficit=20).
  // Now Tenant A has deficit 20 >= cost 20 (ready to run A1 on next pass).
  // Fast-forward must not skip rounds or inflate Tenant A's deficit.
  // Pass 2: Tenant A runs A1 (cost 20, deficit becomes 0).
  // Next in Tenant A is A2 (cost 100 > def 0). Total cost = 20.
  auto slice = queue.popSlice(30);
  for (auto& cb : slice.callbacks) {
    cb();
  }
  EXPECT_EQ(order, (std::vector<std::string>{"A1"}));
  EXPECT_TRUE(slice.has_more);
}

} // namespace
} // namespace Event
} // namespace Envoy
