#include "domain/plan.hpp"

#include <userver/utest/utest.hpp>

namespace {

using namespace factoryctl::domain;

const ItemKey kDust{"gregtech:gt.metaitem.01", 2017, ""};
const ItemKey kRod{"gregtech:gt.metaitem.01", 23324, ""};
const ItemKey kDrop{"ae2fc:fluid_drop", 0, "molten.platinum"};

auto run(const std::vector<Target>& targets, const Stock* stock,
         const std::vector<Order>& orders = {},
         const std::vector<Cpu>& cpus = {}) -> std::vector<NewOrder> {
  return plan({targets, stock, orders, cpus});
}

TEST(Plan, BelowThresholdOrdersBatch) {
  const Stock stock{{kDust, 99}};
  EXPECT_EQ(run({{kDust, 100, 500}}, &stock),
            (std::vector<NewOrder>{{kDust, 500}}));
}

TEST(Plan, AtOrAboveThresholdOrdersNothing) {
  const Stock stock{{kDust, 100}};
  EXPECT_TRUE(run({{kDust, 100, 500}}, &stock).empty());
}

TEST(Plan, AbsentFromScanCountsAsZero) {
  const Stock stock{};
  EXPECT_EQ(run({{kDust, 1, 64}}, &stock),
            (std::vector<NewOrder>{{kDust, 64}}));
}

TEST(Plan, NoCompleteScanOrdersNothing) {
  // Without a scan the stock is unknown, not zero.
  EXPECT_TRUE(run({{kDust, 100, 500}}, nullptr).empty());
}

TEST(Plan, ZeroThresholdOrdersAlways) {
  const Stock stock{{kDust, 1'000'000}};
  EXPECT_EQ(run({{kDust, 0, 500}}, &stock),
            (std::vector<NewOrder>{{kDust, 500}}));
}

TEST(Plan, ZeroThresholdWaitsForTheScanToo) {
  EXPECT_TRUE(run({{kDust, 0, 500}}, nullptr).empty());
}

TEST(Plan, OpenOrderBlocksAnother) {
  const Stock stock{};
  for (const auto status : {OrderStatus::kQueued, OrderStatus::kSent,
                            OrderStatus::kAccepted, OrderStatus::kCrafting}) {
    EXPECT_TRUE(run({{kDust, 100, 500}}, &stock, {{kDust, status}}).empty())
        << static_cast<int>(status);
  }
}

TEST(Plan, ClosedOrderDoesNotBlock) {
  const Stock stock{};
  for (const auto status : {OrderStatus::kDone, OrderStatus::kFailed,
                            OrderStatus::kCanceled, OrderStatus::kLost}) {
    EXPECT_EQ(run({{kDust, 100, 500}}, &stock, {{kDust, status}}).size(), 1)
        << static_cast<int>(status);
  }
}

TEST(Plan, OrderOfAnotherItemDoesNotBlock) {
  const Stock stock{};
  EXPECT_EQ(
      run({{kDust, 100, 500}}, &stock, {{kRod, OrderStatus::kQueued}}).size(),
      1);
}

TEST(Plan, CraftingByCpuBlocks) {
  // A player may have started it by hand: do not order it twice.
  const Stock stock{};
  EXPECT_TRUE(run({{kDust, 100, 500}}, &stock, {}, {{true, kDust}}).empty());
}

TEST(Plan, CraftingByCpuBlocksEvenIfNotBusy) {
  // busy=false with an output shown: a craft that has just started.
  const Stock stock{};
  EXPECT_TRUE(run({{kDust, 100, 500}}, &stock, {}, {{false, kDust}}).empty());
}

TEST(Plan, LowPriorityWaitsForForeignWorkEvenIfNotBusy) {
  const ItemKey foreign{"minecraft:diamond", 0, ""};
  const Stock stock{};
  EXPECT_TRUE(
      run({{kDust, 100, 500, Priority::kLow}}, &stock, {}, {{false, foreign}})
          .empty());
}

TEST(Plan, DropsAreToldApartByFluid) {
  const ItemKey otherDrop{"ae2fc:fluid_drop", 0, "molten.iridium"};
  const Stock stock{{otherDrop, 1'000'000}};
  EXPECT_EQ(
      run({{kDrop, 1000, 144}}, &stock, {{otherDrop, OrderStatus::kSent}}),
      (std::vector<NewOrder>{{kDrop, 144}}));
}

TEST(Plan, NonPositiveBatchIsSkipped) {
  const Stock stock{};
  EXPECT_TRUE(run({{kDust, 100, 0}, {kRod, 100, -5}}, &stock).empty());
}

TEST(Plan, KeepsTheOrderOfTargets) {
  const Stock stock{};
  EXPECT_EQ(run({{kRod, 1, 2}, {kDust, 1, 3}}, &stock),
            (std::vector<NewOrder>{{kRod, 2}, {kDust, 3}}));
}

// Low priority runs only when the CPUs are free of other work: idle, or
// busy with items we keep stocked ourselves.
TEST(Plan, LowPriorityWithIdleCpus) {
  const Stock stock{};
  EXPECT_EQ(run({{kDust, 100, 500, Priority::kLow}}, &stock, {}, {{false, {}}})
                .size(),
            1);
}

TEST(Plan, LowPriorityWithCpusOnOurTargets) {
  const Stock stock{{kRod, 1'000}};
  const std::vector<Target> targets{{kRod, 100, 10},
                                    {kDust, 100, 500, Priority::kLow}};
  EXPECT_EQ(run(targets, &stock, {}, {{true, kRod}}),
            (std::vector<NewOrder>{{kDust, 500}}));
}

TEST(Plan, LowPriorityWaitsForForeignWork) {
  const ItemKey foreign{"minecraft:diamond", 0, ""};
  const Stock stock{};
  EXPECT_TRUE(
      run({{kDust, 100, 500, Priority::kLow}}, &stock, {}, {{true, foreign}})
          .empty());
}

TEST(Plan, LowPriorityWaitsForBusyCpuThatHidesItsWork) {
  // Busy without a crafting monitor: we cannot tell what it does.
  const Stock stock{};
  EXPECT_TRUE(run({{kDust, 100, 500, Priority::kLow}}, &stock, {}, {{true, {}}})
                  .empty());
}

TEST(Plan, HighPriorityIgnoresForeignWork) {
  const ItemKey foreign{"minecraft:diamond", 0, ""};
  const Stock stock{};
  EXPECT_EQ(run({{kDust, 100, 500}}, &stock, {}, {{true, foreign}}).size(), 1);
}

}  // namespace
