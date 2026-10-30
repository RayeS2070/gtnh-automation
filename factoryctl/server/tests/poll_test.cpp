#include "domain/poll.hpp"

#include <userver/utest/utest.hpp>

namespace {

using namespace factoryctl::domain;
using namespace std::chrono_literals;

const ItemKey kStick{"minecraft:stick", 0, ""};
const ItemKey kRod{"gregtech:gt.metaitem.01", 23324, ""};
const TimePoint t0{std::chrono::sys_days{std::chrono::October / 30 / 2026}};
const Settings kSettings{};

auto order(std::int64_t id, OrderStatus status, ItemKey item = kStick)
    -> OrderState {
  return {.id = id,
          .item = std::move(item),
          .amount = 1000,
          .status = status,
          .changedAt = t0};
}

auto onCpu(const ItemKey& item, std::int64_t remaining) -> Cpu {
  return {.busy = true,
          .crafting = item,
          .name = "CPU_022",
          .remaining = remaining};
}

// --- when to scan ------------------------------------------------------------

TEST(Poll, FirstScanRightAway) { EXPECT_TRUE(needScan({}, kSettings, t0)); }

TEST(Poll, ScanEveryPeriod) {
  const ScanState s{.lastFinished = t0};
  EXPECT_FALSE(needScan(s, kSettings, t0 + kSettings.scanEvery - 1s));
  EXPECT_TRUE(needScan(s, kSettings, t0 + kSettings.scanEvery));
}

TEST(Poll, NoSecondScanWhileOneRuns) {
  const ScanState s{.lastFinished = t0 - 1h, .running = t0};
  EXPECT_FALSE(needScan(s, kSettings, t0 + 1min));
}

TEST(Poll, LostScanIsReplaced) {
  const ScanState s{.lastFinished = t0 - 1h, .running = t0};
  EXPECT_TRUE(needScan(s, kSettings, t0 + kSettings.scanGiveUp + 1s));
}

TEST(Poll, ScanPeriodIsASetting) {
  Settings fast;
  fast.scanEvery = 10s;
  EXPECT_TRUE(needScan({.lastFinished = t0}, fast, t0 + 10s));
}

// --- one tick
// ------------------------------------------------------------------

TEST(Poll, ResultsBeforeCpus) {
  // The agent canceled the craft and the CPU is free in the same report: the
  // order is canceled, not done.
  std::vector orders{order(1, OrderStatus::kCrafting)};
  orders[0].cpu = "CPU_022";
  const TickInput in{
      .report = {.cpus = {}, .results = {{1, AgentStatus::kCanceled, ""}}},
      .now = t0};
  tick(orders, in, kSettings);
  EXPECT_EQ(orders[0].status, OrderStatus::kCanceled);
}

TEST(Poll, AcceptedAndOnCpuInOneReport) {
  std::vector orders{order(1, OrderStatus::kSent)};
  const TickInput in{.report = {.cpus = {onCpu(kStick, 996)},
                                .results = {{1, AgentStatus::kAccepted, ""}}},
                     .now = t0};
  tick(orders, in, kSettings);
  EXPECT_EQ(orders[0].status, OrderStatus::kCrafting);
  EXPECT_EQ(orders[0].remaining, 996);
}

TEST(Poll, ResultForUnknownOrderIsAnError) {
  std::vector<OrderState> orders;
  const TickInput in{.report = {.results = {{42, AgentStatus::kDone, ""}}},
                     .now = t0};
  const auto out = tick(orders, in, kSettings);
  ASSERT_EQ(out.errors.size(), 1);
  EXPECT_NE(out.errors[0].find("42"), std::string::npos) << out.errors[0];
}

TEST(Poll, ResultThatDoesNotFitIsAnError) {
  std::vector orders{order(1, OrderStatus::kCrafting)};
  const TickInput in{.report = {.cpus = {onCpu(kStick, 5)},
                                .results = {{1, AgentStatus::kFailed, ""}}},
                     .now = t0};
  const auto out = tick(orders, in, kSettings);
  EXPECT_EQ(out.errors.size(), 1);
  EXPECT_EQ(orders[0].status, OrderStatus::kCrafting);
}

TEST(Poll, QueuedOrdersAreSent) {
  std::vector orders{order(1, OrderStatus::kQueued),
                     order(2, OrderStatus::kSent, kRod)};
  const auto out = tick(orders, {.now = t0}, kSettings);
  EXPECT_EQ(out.send, (std::vector<std::int64_t>{1}));
  EXPECT_EQ(orders[0].status, OrderStatus::kSent);
}

TEST(Poll, CancelTasksForCraftsOnCpus) {
  std::vector orders{order(1, OrderStatus::kCrafting),
                     order(2, OrderStatus::kSent, kRod)};
  orders[0].cancelRequested = true;
  orders[1].cancelRequested = true;  // not on a CPU yet: no task
  const TickInput in{.report = {.cpus = {onCpu(kStick, 500)}}, .now = t0};
  const auto out = tick(orders, in, kSettings);
  EXPECT_EQ(out.cancel, (std::vector<CancelTask>{{1, "CPU_022"}}));
}

TEST(Poll, TimeLimitsGiveAlarms) {
  std::vector orders{order(1, OrderStatus::kSent)};
  const auto late = t0 + kSettings.timeouts.sent + 1s;
  const auto out = tick(orders, {.now = late}, kSettings);
  EXPECT_EQ(out.alarms, (std::vector<Alarm>{{1, AlarmKind::kLost}}));
  EXPECT_EQ(orders[0].status, OrderStatus::kLost);
}

TEST(Poll, PlanSeesTheOrdersAfterTheReport) {
  // The stick order finishes in this report: a new one may be planned now.
  std::vector orders{order(1, OrderStatus::kCrafting)};
  orders[0].cpu = "CPU_022";
  const Stock stock{{kStick, 10}};
  const TickInput in{.report = {.cpus = {}},
                     .targets = {{kStick, 100, 1000}},
                     .stock = &stock,
                     .now = t0};
  const auto out = tick(orders, in, kSettings);
  EXPECT_EQ(orders[0].status, OrderStatus::kDone);
  EXPECT_EQ(out.created, (std::vector<NewOrder>{{kStick, 1000}}));
}

TEST(Poll, OpenOrderStillBlocksPlanning) {
  std::vector orders{order(1, OrderStatus::kSent)};
  const Stock stock{};
  const TickInput in{
      .targets = {{kStick, 100, 1000}}, .stock = &stock, .now = t0};
  EXPECT_TRUE(tick(orders, in, kSettings).created.empty());
}

TEST(Poll, LowPriorityReserveIsASetting) {
  std::vector<OrderState> orders;
  const Stock stock{};
  const TickInput in{.report = {.cpus = {Cpu{}, Cpu{}}},
                     .targets = {{kStick, 100, 1000, Priority::kLow}},
                     .stock = &stock,
                     .now = t0};
  EXPECT_TRUE(tick(orders, in, kSettings).created.empty()) << "default: 8 idle";
  Settings two;
  two.lowFreeCpus = 2;
  EXPECT_EQ(tick(orders, in, two).created.size(), 1);
}

TEST(Poll, NewOrdersWaitForTheNextPoll) {
  std::vector<OrderState> orders;
  const Stock stock{};
  const TickInput in{
      .targets = {{kStick, 100, 1000}}, .stock = &stock, .now = t0};
  const auto out = tick(orders, in, kSettings);
  EXPECT_EQ(out.created.size(), 1);
  EXPECT_TRUE(out.send.empty()) << "a new order has no id before it is stored";
}

}  // namespace
