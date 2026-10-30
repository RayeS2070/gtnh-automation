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

// Low priority takes only idle CPUs and leaves a reserve of them for
// everything else (the maintainer's rule, "no CPU does foreign work", never
// let it run in a busy network: 11 of 32 CPUs were always busy).
const ItemKey kForeign{"minecraft:diamond", 0, ""};

auto cpus(int idle, int busy) -> std::vector<Cpu> {
  std::vector<Cpu> out(idle);
  for (int i = 0; i < busy; ++i) out.push_back({true, kForeign});
  return out;
}

auto runLow(const std::vector<Cpu>& cpus, std::int32_t reserve) {
  const Stock stock{};
  const std::vector<Target> targets{{kDust, 100, 500, Priority::kLow}};
  return plan({targets, &stock, {}, cpus, reserve});
}

TEST(Plan, LowPriorityWithEnoughIdleCpus) {
  EXPECT_EQ(runLow(cpus(8, 24), 8).size(), 1) << "8 idle, reserve 8";
}

TEST(Plan, LowPriorityWaitsBelowTheReserve) {
  EXPECT_TRUE(runLow(cpus(7, 25), 8).empty());
}

TEST(Plan, LowPriorityDoesNotCareWhatBusyCpusDo) {
  EXPECT_EQ(runLow(cpus(21, 11), 8).size(), 1) << "the case seen in game";
}

TEST(Plan, CpuShowingAnOutputIsNotIdle) {
  // busy=false with an output: a craft just started (the race seen in game).
  std::vector<Cpu> c = cpus(8, 0);
  c[0].crafting = kForeign;
  EXPECT_TRUE(runLow(c, 8).empty());
}

TEST(Plan, BusyCpuWithoutAnOutputIsNotIdle) {
  // No crafting monitor: busy all the same.
  std::vector<Cpu> c = cpus(8, 0);
  c[0].busy = true;
  EXPECT_TRUE(runLow(c, 8).empty());
}

TEST(Plan, NoCpusReportedMeansNoneIdle) {
  // getCpus() failed on the agent: an empty list is not "all idle".
  EXPECT_TRUE(runLow({}, 1).empty());
}

TEST(Plan, ReserveZeroAlwaysAllowsLow) { EXPECT_EQ(runLow({}, 0).size(), 1); }

TEST(Plan, HighPriorityIgnoresTheReserve) {
  const Stock stock{};
  const std::vector<Target> targets{{kDust, 100, 500}};
  EXPECT_EQ(plan({targets, &stock, {}, cpus(0, 32), 8}).size(), 1);
}

}  // namespace
