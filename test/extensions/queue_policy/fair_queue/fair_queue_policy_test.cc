#include <chrono>
#include <string>
#include <type_traits>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/queue_policy/fair_queue/v3/fair_queue.pb.h"
#include "envoy/type/metadata/v3/metadata.pb.h"

#include "source/common/config/metadata.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/queue_policy/fair_queue/fair_queue_policy.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {
namespace {

using FairQueuePolicyConfig =
    envoy::extensions::queue_policy::fair_queue::v3::FairQueuePolicyConfig;
using ::testing::NiceMock;
using ::testing::ReturnRef;

constexpr absl::string_view FilterName = "envoy.filters.http.header_to_metadata";
constexpr absl::string_view KeyName = "tenant_id";

// Lightweight item wrapper used to test FairQueue behavior.
class TestQueueItem {
public:
  explicit TestQueueItem(int id) : id_(id) {}
  int id() const { return id_; }

private:
  int id_;
};

// Builds a MetadataKey proto and wrapper for FilterName / KeyName.
Envoy::Config::MetadataKey makeTenantMetadataKey() {
  envoy::type::metadata::v3::MetadataKey proto_key;
  proto_key.set_key(std::string(FilterName));
  proto_key.add_path()->set_key(std::string(KeyName));
  return Envoy::Config::MetadataKey(proto_key);
}

// Holds a MockStreamInfo with its backing metadata_ proto populated for tests.
class TenantStreamInfo {
public:
  // Creates a StreamInfo with no tenant metadata populated.
  TenantStreamInfo() = default;

  // Creates a StreamInfo with the specified Protobuf::Value stored at FilterName / KeyName.
  explicit TenantStreamInfo(const Protobuf::Value& value) {
    (*(*stream_info_.metadata_.mutable_filter_metadata())[std::string(FilterName)]
          .mutable_fields())[std::string(KeyName)] = value;
  }

  // Creates a StreamInfo with a string tenant identifier.
  explicit TenantStreamInfo(absl::string_view tenant_id)
      : TenantStreamInfo(ValueUtil::stringValue(tenant_id)) {}

  // Returns an OptRef to the underlying StreamInfo.
  OptRef<const StreamInfo::StreamInfo> optRef() const {
    return makeOptRef<const StreamInfo::StreamInfo>(stream_info_);
  }

private:
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

// Creates a MonotonicTime from milliseconds since epoch.
MonotonicTime timeFromMs(int64_t ms) { return MonotonicTime(std::chrono::milliseconds(ms)); }

// Drains all items from the queue using peek() and pop(), returning their IDs in dequeue order.
std::vector<int> drainQueueIds(FairQueue<TestQueueItem>& queue) {
  std::vector<int> drained;
  while (!queue.empty()) {
    drained.push_back(queue.peek().id());
    queue.pop();
  }
  return drained;
}

TEST(FairQueueTest, BasicQueueOperationsAndRoundRobinFairness) {
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/1024);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
  EXPECT_FALSE(queue.isOverloaded());

  // Enqueue 3 items for tenant_a, 2 for tenant_b, and 1 for tenant_c.
  TenantStreamInfo tenant_a("tenant_a");
  TenantStreamInfo tenant_b("tenant_b");
  TenantStreamInfo tenant_c("tenant_c");

  TestQueueItem a1(1), a2(2), a3(3);
  TestQueueItem b1(10), b2(20);
  TestQueueItem c1(100);

  queue.add(a1, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a2, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a3, {timeFromMs(0), tenant_a.optRef()});
  queue.add(b1, {timeFromMs(0), tenant_b.optRef()});
  queue.add(b2, {timeFromMs(0), tenant_b.optRef()});
  queue.add(c1, {timeFromMs(0), tenant_c.optRef()});

  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.size(), 6);

  static_assert(std::is_same_v<decltype(queue.peek()), TestQueueItem&>);
  const FairQueue<TestQueueItem>& const_queue = queue;
  static_assert(std::is_same_v<decltype(const_queue.peek()), const TestQueueItem&>);
  EXPECT_EQ(const_queue.peek().id(), 1);

  // Dequeue order must interleave tenants in round-robin rounds:
  // Round 1: a1 (1), b1 (10), c1 (100)
  // Round 2: a2 (2), b2 (20)
  // Round 3: a3 (3)
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 10, 100, 2, 20, 3}));
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
}

TEST(FairQueueTest, MissingAndNullMetadataShareSingleDefaultClass) {
  // Use 2 buckets and pick a tagged tenant that hashes to the bucket opposite the default class.
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/2);

  TenantStreamInfo missing_key_info;
  TenantStreamInfo null_value_info(ValueUtil::nullValue());
  // Find a tenant string that lands in the other bucket from the default class at seed 0.
  const uint64_t default_bucket = HashUtil::xxHash64Value(uint32_t{0}, /*seed=*/0) % 2;
  std::string tagged_name = "tenant_0";
  for (int i = 0; i < 100; ++i) {
    std::string candidate = absl::StrCat("tenant_", i);
    const uint64_t type_seed =
        HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), /*seed=*/0);
    if ((HashUtil::xxHash64(candidate, type_seed) % 2) != default_bucket) {
      tagged_name = candidate;
      break;
    }
  }
  TenantStreamInfo tagged_info(tagged_name);

  TestQueueItem untagged_no_stream_info(1);
  TestQueueItem untagged_missing_key(2);
  TestQueueItem untagged_null_value(3);
  TestQueueItem tagged_item(99);

  // Enqueue all three untagged variants first, then the tagged item.
  queue.add(untagged_no_stream_info, {timeFromMs(0), {}});
  queue.add(untagged_missing_key, {timeFromMs(0), missing_key_info.optRef()});
  queue.add(untagged_null_value, {timeFromMs(0), null_value_info.optRef()});
  queue.add(tagged_item, {timeFromMs(0), tagged_info.optRef()});

  // Because all three untagged items share one default bucket, the tagged item is served second
  // rather than fourth.
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 99, 2, 3}));
}

TEST(FairQueueTest, SupportsAllProtobufValueKinds) {
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/1024);

  Protobuf::Struct struct_val;
  (*struct_val.mutable_fields())["nested"] = ValueUtil::stringValue("v");

  TenantStreamInfo number_info(ValueUtil::numberValue(42.0));
  TenantStreamInfo bool_info(ValueUtil::boolValue(true));
  TenantStreamInfo struct_info(ValueUtil::structValue(struct_val));
  TenantStreamInfo list_info(
      ValueUtil::listValue({ValueUtil::stringValue("a"), ValueUtil::numberValue(1)}));

  TestQueueItem i1(1), i2(2), i3(3), i4(4);
  queue.add(i1, {timeFromMs(0), number_info.optRef()});
  queue.add(i2, {timeFromMs(0), bool_info.optRef()});
  queue.add(i3, {timeFromMs(0), struct_info.optRef()});
  queue.add(i4, {timeFromMs(0), list_info.optRef()});

  EXPECT_EQ(queue.size(), 4);
  const std::vector<int> drained = drainQueueIds(queue);
  EXPECT_EQ(drained.size(), 4);
}

TEST(FairQueueTest, RemoveMidQueueAndBucketDeactivation) {
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/1024);

  TenantStreamInfo tenant_a("tenant_a");
  TenantStreamInfo tenant_b("tenant_b");

  TestQueueItem a1(1), a2(2);
  TestQueueItem b1(10);

  queue.add(a1, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a2, {timeFromMs(0), tenant_a.optRef()});
  queue.add(b1, {timeFromMs(0), tenant_b.optRef()});

  // Removing the head item of tenant_a leaves a2 in tenant_a's bucket without losing its turn.
  queue.remove(a1);
  EXPECT_EQ(queue.size(), 2);
  EXPECT_EQ(queue.peek().id(), 2);

  // Removing b1 empties tenant_b's bucket and deactivates it.
  queue.remove(b1);
  EXPECT_EQ(queue.size(), 1);
  EXPECT_EQ(queue.peek().id(), 2);

  queue.remove(a2);
  EXPECT_TRUE(queue.empty());
}

TEST(FairQueueTest, ForEachVisitsInRoundRobinOrderAndSupportsRemove) {
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/1024);

  // Empty queue forEach is a no-op.
  int empty_visits = 0;
  queue.forEach([&empty_visits](TestQueueItem&) -> bool {
    ++empty_visits;
    return true;
  });
  EXPECT_EQ(empty_visits, 0);

  TenantStreamInfo tenant_a("tenant_a");
  TenantStreamInfo tenant_b("tenant_b");
  TestQueueItem a1(1), a2(2);
  TestQueueItem b1(10), b2(20);

  queue.add(a1, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a2, {timeFromMs(0), tenant_a.optRef()});
  queue.add(b1, {timeFromMs(0), tenant_b.optRef()});
  queue.add(b2, {timeFromMs(0), tenant_b.optRef()});

  // Full traversal visits items in round-robin dequeue order: a1, b1, a2, b2.
  std::vector<int> visited;
  queue.forEach([&visited](TestQueueItem& item) -> bool {
    visited.push_back(item.id());
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{1, 10, 2, 20}));

  // Returning false stops iteration early.
  visited.clear();
  queue.forEach([&visited](TestQueueItem& item) -> bool {
    visited.push_back(item.id());
    return item.id() != 10;
  });
  EXPECT_EQ(visited, (std::vector<int>{1, 10}));

  // Removing visited items during forEach is safe.
  visited.clear();
  queue.forEach([&queue, &visited](TestQueueItem& item) -> bool {
    visited.push_back(item.id());
    if (item.id() == 10) {
      queue.remove(item);
    }
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{1, 10, 2, 20}));
  EXPECT_EQ(queue.size(), 3);
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 20, 2}));
}

TEST(FairQueueTest, PerturbationSeparatesCollidingTenantsAfterInterval) {
  // Find two tenants that collide at seed 0 in a 2-bucket table, and a third tenant in the
  // opposite bucket at seed 0, such that the two colliding tenants separate at seed 1.
  const uint64_t seed0_type =
      HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), /*seed=*/0);
  const uint64_t seed1_type =
      HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), /*seed=*/1);

  std::string colliding_a;
  std::string colliding_b;
  std::string other_seed0;

  for (int i = 0; i < 200; ++i) {
    const std::string candidate = absl::StrCat("flow_", i);
    const uint64_t b0 = HashUtil::xxHash64(candidate, seed0_type) % 2;
    const uint64_t b1 = HashUtil::xxHash64(candidate, seed1_type) % 2;
    if (colliding_a.empty() && b0 == 0 && b1 == 0) {
      colliding_a = candidate;
    } else if (colliding_b.empty() && b0 == 0 && b1 == 1) {
      colliding_b = candidate;
    } else if (other_seed0.empty() && b0 == 1) {
      other_seed0 = candidate;
    }
  }
  ASSERT_FALSE(colliding_a.empty());
  ASSERT_FALSE(colliding_b.empty());
  ASSERT_FALSE(other_seed0.empty());

  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/2,
                                 std::chrono::milliseconds(1000));

  TenantStreamInfo info_a(colliding_a);
  TenantStreamInfo info_b(colliding_b);
  TenantStreamInfo info_other(other_seed0);

  // At t = 0ms (seed 0), colliding_a and colliding_b land in bucket 0, while other_seed0 lands
  // in bucket 1. Thus other_seed0 is dequeued before colliding_b.
  TestQueueItem item_a(1), item_b(2), item_other(3);
  queue.add(item_a, {timeFromMs(0), info_a.optRef()});
  queue.add(item_b, {timeFromMs(500), info_b.optRef()});
  queue.add(item_other, {timeFromMs(500), info_other.optRef()});
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 3, 2}));

  // At t = 1000ms (>= perturbation_interval), the seed advances to 1. Now colliding_a (bucket 0)
  // and colliding_b (bucket 1) land in different buckets and interleave fairly.
  TestQueueItem item_a1(11), item_a2(12), item_b1(21);
  queue.add(item_a1, {timeFromMs(1000), info_a.optRef()});
  queue.add(item_a2, {timeFromMs(1000), info_a.optRef()});
  queue.add(item_b1, {timeFromMs(1000), info_b.optRef()});
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{11, 21, 12}));
}

TEST(FairQueueTest, ZeroPerturbationIntervalDisablesSeedRotation) {
  const uint64_t seed0_type =
      HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), /*seed=*/0);
  std::string colliding_a;
  std::string colliding_b;
  std::string other_seed0;
  for (int i = 0; i < 200; ++i) {
    const std::string candidate = absl::StrCat("flow_", i);
    const uint64_t b0 = HashUtil::xxHash64(candidate, seed0_type) % 2;
    if (colliding_a.empty() && b0 == 0) {
      colliding_a = candidate;
    } else if (colliding_b.empty() && b0 == 0) {
      colliding_b = candidate;
    } else if (other_seed0.empty() && b0 == 1) {
      other_seed0 = candidate;
    }
  }

  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/2,
                                 std::chrono::milliseconds(0));

  TenantStreamInfo info_a(colliding_a);
  TenantStreamInfo info_b(colliding_b);
  TenantStreamInfo info_other(other_seed0);

  TestQueueItem item_a(1), item_b(2), item_other(3);
  queue.add(item_a, {timeFromMs(0), info_a.optRef()});
  queue.add(item_b, {timeFromMs(5000), info_b.optRef()});
  queue.add(item_other, {timeFromMs(5000), info_other.optRef()});
  // Even after 5000ms, seed remains 0 so colliding_a and colliding_b stay in bucket 0.
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 3, 2}));
}

class FairQueueFactoryTest : public ::testing::Test {
protected:
  FairQueueFactory<TestQueueItem> factory_;
};

TEST_F(FairQueueFactoryTest, FactoryMetadataAndEmptyProto) {
  EXPECT_EQ(factory_.name(), "envoy.queue_policy.fair_queue");
  EXPECT_EQ(factory_.category(), "envoy.queue_policy");
  EXPECT_NE(factory_.createEmptyConfigProto(), nullptr);
}

TEST_F(FairQueueFactoryTest, CreateQueuePolicyWithDefaultsAndCustomConfig) {
  FairQueuePolicyConfig config;
  config.mutable_class_metadata_key()->set_key(std::string(FilterName));
  config.mutable_class_metadata_key()->add_path()->set_key(std::string(KeyName));

  auto default_result = factory_.createQueuePolicy(config, "test_prefix",
                                                   ProtobufMessage::getStrictValidationVisitor());
  EXPECT_TRUE(default_result.ok());
  EXPECT_NE(default_result.value(), nullptr);

  config.mutable_num_buckets()->set_value(256);
  config.mutable_perturbation_interval()->set_seconds(2);
  auto custom_result = factory_.createQueuePolicy(config, "test_prefix",
                                                  ProtobufMessage::getStrictValidationVisitor());
  EXPECT_TRUE(custom_result.ok());
  EXPECT_NE(custom_result.value(), nullptr);
}

TEST_F(FairQueueFactoryTest, ValidationRejectsMissingMetadataKeyOrZeroBuckets) {
  FairQueuePolicyConfig empty_config;
  EXPECT_THROW(factory_
                   .createQueuePolicy(empty_config, "test_prefix",
                                      ProtobufMessage::getStrictValidationVisitor())
                   .IgnoreError(),
               ProtoValidationException);

  FairQueuePolicyConfig zero_buckets_config;
  zero_buckets_config.mutable_class_metadata_key()->set_key(std::string(FilterName));
  zero_buckets_config.mutable_class_metadata_key()->add_path()->set_key(std::string(KeyName));
  zero_buckets_config.mutable_num_buckets()->set_value(0);
  EXPECT_THROW(factory_
                   .createQueuePolicy(zero_buckets_config, "test_prefix",
                                      ProtobufMessage::getStrictValidationVisitor())
                   .IgnoreError(),
               ProtoValidationException);
}

} // namespace
} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
