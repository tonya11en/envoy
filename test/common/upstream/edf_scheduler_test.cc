#include "source/common/upstream/edf_scheduler.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

class EdfSchedulerTest : public testing::Test {
public:
  template <typename T>
  static void compareEdfScehdulers(EdfScheduler<T>& scheduler1, EdfScheduler<T>& scheduler2) {
    // Compares that the given EdfSchedulers internal queues are equal up
    // (ignoring the order_offset_ values).
    EXPECT_EQ(scheduler1.queue_.size(), scheduler2.queue_.size());
    // Cannot iterate over std::priority_queue directly, so need to copy the
    // contents to a vector first.
    // std::function<std::vector<EdfScheduler<T>::EdfEntry>(EdfScheduler<T>& scheduler)>
    auto copyFunc = [](EdfScheduler<T>& scheduler) {
      std::vector<typename EdfScheduler<T>::EdfEntry> result;
      result.reserve(scheduler.queue_.size());
      while (!scheduler.empty()) {
        result.emplace_back(std::move(scheduler.queue_.top()));
        scheduler.queue_.pop();
      }
      // Re-add all elements so the contents of the input scheduler isn't
      // changed.
      for (auto& entry : result) {
        scheduler.queue_.push(entry);
      }
      return result;
    };
    std::vector<typename EdfScheduler<T>::EdfEntry> contents1 = copyFunc(scheduler1);
    std::vector<typename EdfScheduler<T>::EdfEntry> contents2 = copyFunc(scheduler2);
    for (size_t i = 0; i < contents1.size(); ++i) {
      EXPECT_NEAR(contents1[i].deadline_, contents2[i].deadline_, 0.0000001)
          << "inequal deadline in element " << i;
      std::shared_ptr<T> entry1 = contents1[i].entry_.lock();
      std::shared_ptr<T> entry2 = contents2[i].entry_.lock();
      EXPECT_EQ(*entry1, *entry2) << "inequal entry in element " << i;
    }
  }
};

TEST_F(EdfSchedulerTest, Empty) {
  EdfScheduler<uint32_t> sched;
  EXPECT_EQ(nullptr, sched.peekAgain([](const double&) { return 0; }));
  EXPECT_EQ(nullptr, sched.pickAndAdd([](const double&) { return 0; }));
}

// Validate we get regular RR behavior when all weights are the same.
TEST_F(EdfSchedulerTest, Unweighted) {
  EdfScheduler<uint32_t> sched;
  constexpr uint32_t num_entries = 128;
  std::shared_ptr<uint32_t> entries[num_entries];

  for (uint32_t i = 0; i < num_entries; ++i) {
    entries[i] = std::make_shared<uint32_t>(i);
    sched.add(1, entries[i]);
  }

  for (uint32_t rounds = 0; rounds < 128; ++rounds) {
    for (uint32_t i = 0; i < num_entries; ++i) {
      auto peek = sched.peekAgain([](const double&) { return 1; });
      auto p = sched.pickAndAdd([](const double&) { return 1; });
      EXPECT_EQ(i, *p);
      EXPECT_EQ(*peek, *p);
    }
  }
}

// Validate we get weighted RR behavior when weights are distinct.
TEST_F(EdfSchedulerTest, Weighted) {
  EdfScheduler<uint32_t> sched;
  constexpr uint32_t num_entries = 128;
  std::shared_ptr<uint32_t> entries[num_entries];
  uint32_t pick_count[num_entries];

  for (uint32_t i = 0; i < num_entries; ++i) {
    entries[i] = std::make_shared<uint32_t>(i);
    sched.add(i + 1, entries[i]);
    pick_count[i] = 0;
  }

  for (uint32_t i = 0; i < (num_entries * (1 + num_entries)) / 2; ++i) {
    auto peek = sched.peekAgain([](const double& orig) { return orig + 1; });
    auto p = sched.pickAndAdd([](const double& orig) { return orig + 1; });
    EXPECT_EQ(*p, *peek);
    ++pick_count[*p];
  }

  for (uint32_t i = 0; i < num_entries; ++i) {
    EXPECT_EQ(i + 1, pick_count[i]);
  }
}

// Validate that expired entries are ignored.
TEST_F(EdfSchedulerTest, Expired) {
  EdfScheduler<uint32_t> sched;

  auto second_entry = std::make_shared<uint32_t>(42);
  {
    auto first_entry = std::make_shared<uint32_t>(37);
    sched.add(2, first_entry);
    sched.add(1, second_entry);
  }

  auto peek = sched.peekAgain([](const double&) { return 1; });
  auto p = sched.pickAndAdd([](const double&) { return 1; });
  EXPECT_EQ(*peek, *p);
  EXPECT_EQ(*second_entry, *p);
  EXPECT_EQ(*second_entry, *p);
}

// Validate that expired entries are not peeked.
TEST_F(EdfSchedulerTest, ExpiredPeek) {
  EdfScheduler<uint32_t> sched;

  {
    auto second_entry = std::make_shared<uint32_t>(42);
    auto first_entry = std::make_shared<uint32_t>(37);
    sched.add(2, first_entry);
    sched.add(1, second_entry);
  }
  auto third_entry = std::make_shared<uint32_t>(37);
  sched.add(3, third_entry);

  EXPECT_EQ(37, *sched.peekAgain([](const double&) { return 1; }));
}

// Validate that expired entries are ignored.
TEST_F(EdfSchedulerTest, ExpiredPeekedIsNotPicked) {
  EdfScheduler<uint32_t> sched;

  {
    auto second_entry = std::make_shared<uint32_t>(42);
    auto first_entry = std::make_shared<uint32_t>(37);
    sched.add(2, first_entry);
    sched.add(1, second_entry);
    for (int i = 0; i < 3; ++i) {
      EXPECT_TRUE(sched.peekAgain([](const double&) { return 1; }) != nullptr);
    }
  }

  EXPECT_TRUE(sched.peekAgain([](const double&) { return 1; }) == nullptr);
  EXPECT_TRUE(sched.pickAndAdd([](const double&) { return 1; }) == nullptr);
}

TEST_F(EdfSchedulerTest, ManyPeekahead) {
  EdfScheduler<uint32_t> sched1;
  EdfScheduler<uint32_t> sched2;
  constexpr uint32_t num_entries = 128;
  std::shared_ptr<uint32_t> entries[num_entries];

  for (uint32_t i = 0; i < num_entries; ++i) {
    entries[i] = std::make_shared<uint32_t>(i);
    sched1.add(1, entries[i]);
    sched2.add(1, entries[i]);
  }

  std::vector<uint32_t> picks;
  for (uint32_t rounds = 0; rounds < 10; ++rounds) {
    picks.push_back(*sched1.peekAgain([](const double&) { return 1; }));
  }
  for (uint32_t rounds = 0; rounds < 10; ++rounds) {
    auto p1 = sched1.pickAndAdd([](const double&) { return 1; });
    auto p2 = sched2.pickAndAdd([](const double&) { return 1; });
    EXPECT_EQ(picks[rounds], *p1);
    EXPECT_EQ(*p2, *p1);
  }
}

// Validates that creating a scheduler using the createWithPicks (with 0 picks)
// is equal to creating an empty scheduler and adding entries one after the other.
TEST_F(EdfSchedulerTest, EqualityAfterCreateEmpty) {
  constexpr uint32_t num_entries = 128;
  std::vector<std::shared_ptr<uint32_t>> entries;
  entries.reserve(num_entries);

  // Populate sched1 one entry after the other.
  EdfScheduler<uint32_t> sched1;
  for (uint32_t i = 0; i < num_entries; ++i) {
    entries.emplace_back(std::make_shared<uint32_t>(i + 1));
    sched1.add(i + 1, entries.back());
  }

  EdfScheduler<uint32_t> sched2 = EdfScheduler<uint32_t>::createWithPicks(
      entries, [](const double& w) { return w; }, 0);

  compareEdfScehdulers(sched1, sched2);
}

// Validates that creating a scheduler using the createWithPicks (with 5 picks)
// is equal to creating an empty scheduler and adding entries one after the other,
// and then performing some number of picks.
TEST_F(EdfSchedulerTest, EqualityAfterCreateWithPicks) {
  constexpr uint32_t num_entries = 128;
  // Use double-precision weights from the range [0.01, 100.5].
  // Using different weights to avoid a case where entries with the same weight
  // will be chosen in different order.
  std::vector<std::shared_ptr<double>> entries;
  entries.reserve(num_entries);
  for (uint32_t i = 0; i < num_entries; ++i) {
    const double entry_weight = (100.5 - 0.01) / num_entries * i + 0.01;
    entries.emplace_back(std::make_shared<double>(entry_weight));
  }

  const std::vector<uint32_t> all_picks{5, 140, 501, 123456, 894571};
  for (const auto picks : all_picks) {
    // Populate sched1 one entry after the other.
    EdfScheduler<double> sched1;
    for (uint32_t i = 0; i < num_entries; ++i) {
      sched1.add(*entries[i], entries[i]);
    }
    // Perform the picks on sched1.
    for (uint32_t i = 0; i < picks; ++i) {
      sched1.pickAndAdd([](const double& w) { return w; });
    }

    // Create sched2 with pre-built and pre-picked entries.
    EdfScheduler<double> sched2 = EdfScheduler<double>::createWithPicks(
        entries, [](const double& w) { return w; }, picks);

    compareEdfScehdulers(sched1, sched2);
  }
}

void firstPickTest(int iterations) {
  std::cout << "Testing first picks. iters=" << iterations << "\n";

  TestRandomGenerator rand;

  std::vector<std::shared_ptr<int>> entries{
      std::make_shared<int>(1337),
      std::make_shared<int>(1338),
  };

  int64_t total_picks = 0;
  std::unordered_map<int, int> s1_picks, s2_picks;

  constexpr double w1 = 25.0;
  constexpr double w2 = 75.0;
  auto calc_weight = [](const int& x) -> double {
    if (x == 1337) {
      return w1;
    }
    return w2;
  };

  for (int ii = 0; ii < iterations; ++ii) {
    ++total_picks;
    uint32_t r = rand.random();

    EdfScheduler<int> s1;
    s1.add(calc_weight(*entries[0]), entries[0]);
    s1.add(calc_weight(*entries[1]), entries[1]);

    auto s2 = EdfScheduler<int>::createWithPicks(entries, calc_weight, r);

    s1_picks[*s1.pickAndAdd(calc_weight)]++;
    s2_picks[*s2.pickAndAdd(calc_weight)]++;
  }

  std::cout << "unrotated EDF:\n";
  for (const auto& it : s1_picks) {
    std::cout << it.first << ": " << 100 * static_cast<double>(it.second) / total_picks << "%\n";
  }

  std::cout << std::endl;
  std::cout << "rotated EDF:\n";
  for (const auto& it : s2_picks) {
    const double expected = calc_weight(it.first);
    const double observed = 100 * static_cast<double>(it.second) / total_picks;
    const double error = 100 * (observed - expected) / expected;
    std::cout << it.first << ":\n"
              << "\tobserved: " << observed << "%\n"
              << "\texpected: " << expected << "%\n"
              << "\trelative_error: " << error << "%\n";
  }
  std::cout << std::endl;
}

TEST_F(EdfSchedulerTest, EdfHax) {
  firstPickTest(1000);
  firstPickTest(10000);
  firstPickTest(100000);
  firstPickTest(1000000);
}

} // namespace Upstream
} // namespace Envoy
