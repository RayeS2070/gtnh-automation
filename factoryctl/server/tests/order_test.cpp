#include "domain/order.hpp"

#include <userver/utest/utest.hpp>

namespace {

using namespace factoryctl::domain;
using namespace std::chrono_literals;

const ItemKey kStick{"minecraft:stick", 0, ""};
const ItemKey kDiamond{"minecraft:diamond", 0, ""};
const TimePoint t0{std::chrono::sys_days{std::chrono::October / 30 / 2026}};
const Timeouts kLimits{};

auto order(OrderStatus status) -> OrderState {
  return {.id = 7,
          .item = kStick,
          .amount = 1000,
          .status = status,
          .changedAt = t0};
}

auto onCpu(std::int64_t remaining, ItemKey item = kStick) -> Cpu {
  return {.busy = true,
          .crafting = item,
          .name = "CPU_022",
          .remaining = remaining};
}

// --- sending ---------------------------------------------------------------

TEST(Order, QueuedIsSentOnce) {
  auto o = order(OrderStatus::kQueued);
  EXPECT_TRUE(markSent(o, t0 + 1s));
  EXPECT_EQ(o.status, OrderStatus::kSent);
  EXPECT_EQ(o.changedAt, t0 + 1s);
  EXPECT_FALSE(markSent(o, t0 + 2s)) << "at most once";
  EXPECT_EQ(o.changedAt, t0 + 1s);
}

TEST(Order, OnlyQueuedCanBeSent) {
  for (const auto s : {OrderStatus::kComputing, OrderStatus::kCrafting,
                       OrderStatus::kDone, OrderStatus::kLost}) {
    auto o = order(s);
    EXPECT_FALSE(markSent(o, t0)) << static_cast<int>(s);
    EXPECT_EQ(o.status, s);
  }
}

// --- results from the agent ------------------------------------------------

TEST(Order, SentMovesOnByResult) {
  const std::pair<AgentStatus, OrderStatus> cases[] = {
      {AgentStatus::kComputing, OrderStatus::kComputing},
      {AgentStatus::kAccepted, OrderStatus::kAccepted},
      {AgentStatus::kFailed, OrderStatus::kFailed},
      {AgentStatus::kCanceled, OrderStatus::kCanceled},
      {AgentStatus::kDone, OrderStatus::kDone},
  };
  for (const auto& [result, expected] : cases) {
    auto o = order(OrderStatus::kSent);
    EXPECT_TRUE(applyResult(o, result, "", t0 + 5s).has_value());
    EXPECT_EQ(o.status, expected);
    EXPECT_EQ(o.changedAt, t0 + 5s);
  }
}

TEST(Order, ComputingThenAccepted) {
  auto o = order(OrderStatus::kComputing);
  EXPECT_TRUE(applyResult(o, AgentStatus::kAccepted, "", t0).has_value());
  EXPECT_EQ(o.status, OrderStatus::kAccepted);
}

TEST(Order, FailureKeepsTheMessage) {
  auto o = order(OrderStatus::kSent);
  EXPECT_TRUE(
      applyResult(o, AgentStatus::kFailed, "ambiguous: 2 craftables", t0)
          .has_value());
  EXPECT_EQ(o.message, "ambiguous: 2 craftables");
}

TEST(Order, SameResultAgainChangesNothing) {
  auto o = order(OrderStatus::kComputing);
  EXPECT_TRUE(applyResult(o, AgentStatus::kComputing, "", t0 + 9s).has_value());
  EXPECT_EQ(o.changedAt, t0) << "status did not change";
}

TEST(Order, ResultForQueuedIsRejected) {
  auto o = order(OrderStatus::kQueued);
  EXPECT_FALSE(applyResult(o, AgentStatus::kDone, "", t0).has_value());
  EXPECT_EQ(o.status, OrderStatus::kQueued);
}

TEST(Order, ResultForClosedIsRejected) {
  for (const auto s : {OrderStatus::kDone, OrderStatus::kFailed,
                       OrderStatus::kCanceled, OrderStatus::kLost}) {
    auto o = order(s);
    EXPECT_FALSE(applyResult(o, AgentStatus::kAccepted, "", t0).has_value());
    EXPECT_EQ(o.status, s);
  }
}

TEST(Order, LateFailureOnCpuIsRejected) {
  // hasFailed() turned true in the middle of a craft that went on fine.
  auto o = order(OrderStatus::kCrafting);
  EXPECT_FALSE(applyResult(o, AgentStatus::kFailed, "", t0).has_value());
  EXPECT_EQ(o.status, OrderStatus::kCrafting);
}

TEST(Order, NoWayBackToComputing) {
  auto o = order(OrderStatus::kAccepted);
  EXPECT_FALSE(applyResult(o, AgentStatus::kComputing, "", t0).has_value());
  EXPECT_EQ(o.status, OrderStatus::kAccepted);
}

TEST(Order, CraftingEndsByResult) {
  for (const auto& [result, expected] :
       {std::pair{AgentStatus::kDone, OrderStatus::kDone},
        std::pair{AgentStatus::kCanceled, OrderStatus::kCanceled}}) {
    auto o = order(OrderStatus::kCrafting);
    EXPECT_TRUE(applyResult(o, result, "", t0).has_value());
    EXPECT_EQ(o.status, expected);
  }
}

// --- CPUs --------------------------------------------------------------------

TEST(Order, SeenOnCpuIsCrafting) {
  for (const auto s :
       {OrderStatus::kSent, OrderStatus::kComputing, OrderStatus::kAccepted}) {
    auto o = order(s);
    const Cpu cpus[] = {onCpu(996)};
    applyCpus(o, cpus, t0 + 3s);
    EXPECT_EQ(o.status, OrderStatus::kCrafting) << static_cast<int>(s);
    EXPECT_EQ(o.cpu, "CPU_022");
    EXPECT_EQ(o.remaining, 996);
    EXPECT_EQ(o.progressAt, t0 + 3s);
  }
}

TEST(Order, CpuShowingOurOutputCraftsItEvenIfNotBusy) {
  // In game: getCpus() said busy=false while finalOutput() already showed the
  // craft (it started between the two calls). The output decides.
  auto o = order(OrderStatus::kAccepted);
  Cpu cpu = onCpu(1'000'000);
  cpu.busy = false;
  const Cpu cpus[] = {cpu};
  applyCpus(o, cpus, t0);
  EXPECT_EQ(o.status, OrderStatus::kCrafting);
}

TEST(Order, OtherItemsOnCpusDoNotMatter) {
  // Only our item on a CPU means our craft; a busy CPU with another item
  // does not keep the order going.
  auto o = order(OrderStatus::kCrafting);
  o.cpu = "CPU_022";
  const Cpu cpus[] = {onCpu(5, kDiamond), {.busy = false}};
  applyCpus(o, cpus, t0);
  EXPECT_EQ(o.status, OrderStatus::kDone);
}

TEST(Order, ProgressIsRemainingGoingDown) {
  auto o = order(OrderStatus::kCrafting);
  o.cpu = "CPU_022";
  o.remaining = 996;
  o.progressAt = t0;
  const Cpu same[] = {onCpu(996)};
  applyCpus(o, same, t0 + 2s);
  EXPECT_EQ(o.progressAt, t0) << "no progress";
  const Cpu less[] = {onCpu(866)};
  applyCpus(o, less, t0 + 4s);
  EXPECT_EQ(o.remaining, 866);
  EXPECT_EQ(o.progressAt, t0 + 4s);
}

TEST(Order, LeftTheCpuIsDone) {
  auto o = order(OrderStatus::kCrafting);
  o.cpu = "CPU_022";
  applyCpus(o, {}, t0 + 7s);
  EXPECT_EQ(o.status, OrderStatus::kDone);
  EXPECT_EQ(o.changedAt, t0 + 7s);
}

TEST(Order, AcceptedThenGoneBeforeSeenIsDone) {
  // In game the craft shows on a CPU right after request(): an accepted order
  // that no CPU shows has already ended (a short craft, between two polls).
  auto o = order(OrderStatus::kAccepted);
  o.changedAt = t0;
  applyCpus(o, {}, t0 + 10s);
  EXPECT_EQ(o.status, OrderStatus::kDone);
}

TEST(Order, LeftTheCpuAfterCancelIsCanceled) {
  auto o = order(OrderStatus::kCrafting);
  o.cpu = "CPU_022";
  o.cancelRequested = true;
  applyCpus(o, {}, t0);
  EXPECT_EQ(o.status, OrderStatus::kCanceled);
}

TEST(Order, SentOrComputingNotOnCpuWait) {
  for (const auto s : {OrderStatus::kSent, OrderStatus::kComputing}) {
    auto o = order(s);
    applyCpus(o, {}, t0 + 10s);
    EXPECT_EQ(o.status, s);
  }
}

TEST(Order, ClosedIgnoresCpus) {
  auto o = order(OrderStatus::kDone);
  const Cpu cpus[] = {onCpu(10)};
  applyCpus(o, cpus, t0);
  EXPECT_EQ(o.status, OrderStatus::kDone);
}

// --- time limits
// ---------------------------------------------------------------

TEST(Order, SentWithoutResultIsLost) {
  auto o = order(OrderStatus::kSent);
  EXPECT_EQ(checkTimeouts(o, kLimits, t0 + kLimits.sent), std::nullopt);
  EXPECT_EQ(o.status, OrderStatus::kSent);
  EXPECT_EQ(checkTimeouts(o, kLimits, t0 + kLimits.sent + 1s),
            AlarmKind::kLost);
  EXPECT_EQ(o.status, OrderStatus::kLost);
}

TEST(Order, LongComputingAlarmsOnce) {
  auto o = order(OrderStatus::kComputing);
  const auto late = t0 + kLimits.computing + 1s;
  EXPECT_EQ(checkTimeouts(o, kLimits, late), AlarmKind::kComputingTooLong);
  EXPECT_EQ(o.status, OrderStatus::kComputing) << "only an alarm";
  EXPECT_EQ(checkTimeouts(o, kLimits, late + 60s), std::nullopt);
}

TEST(Order, StallAlarmsOnceAndRearmsOnProgress) {
  auto o = order(OrderStatus::kCrafting);
  o.cpu = "CPU_022";
  o.remaining = 500;
  o.progressAt = t0;
  const auto stall = t0 + kLimits.stalled + 1s;
  EXPECT_EQ(checkTimeouts(o, kLimits, t0 + kLimits.stalled), std::nullopt);
  EXPECT_EQ(checkTimeouts(o, kLimits, stall), AlarmKind::kStalled);
  EXPECT_EQ(checkTimeouts(o, kLimits, stall + 60s), std::nullopt);

  const Cpu cpus[] = {onCpu(400)};
  applyCpus(o, cpus, stall + 120s);
  const auto again = stall + 120s + kLimits.stalled + 1s;
  EXPECT_EQ(checkTimeouts(o, kLimits, again), AlarmKind::kStalled);
}

TEST(Order, OtherStatusesHaveNoLimits) {
  for (const auto s : {OrderStatus::kQueued, OrderStatus::kAccepted,
                       OrderStatus::kDone, OrderStatus::kLost}) {
    auto o = order(s);
    EXPECT_EQ(checkTimeouts(o, kLimits, t0 + 24h), std::nullopt);
    EXPECT_EQ(o.status, s);
  }
}

// --- cancel
// --------------------------------------------------------------------

TEST(Order, CancelQueuedRightAway) {
  auto o = order(OrderStatus::kQueued);
  EXPECT_EQ(requestCancel(o, t0 + 1s), CancelAction::kCanceledNow);
  EXPECT_EQ(o.status, OrderStatus::kCanceled);
  EXPECT_FALSE(needsCancelTask(o));
}

TEST(Order, CancelInProgressAsksTheAgent) {
  for (const auto s : {OrderStatus::kSent, OrderStatus::kComputing,
                       OrderStatus::kAccepted, OrderStatus::kCrafting}) {
    auto o = order(s);
    EXPECT_EQ(requestCancel(o, t0), CancelAction::kAskAgent);
    EXPECT_TRUE(o.cancelRequested);
    EXPECT_EQ(o.status, s) << "canceled only when the agent says so";
    EXPECT_EQ(requestCancel(o, t0), CancelAction::kNone) << "asked before";
  }
}

TEST(Order, CancelClosedDoesNothing) {
  auto o = order(OrderStatus::kDone);
  EXPECT_EQ(requestCancel(o, t0), CancelAction::kNone);
  EXPECT_FALSE(o.cancelRequested);
}

TEST(Order, CancelTaskNeedsTheCpu) {
  auto o = order(OrderStatus::kAccepted);
  requestCancel(o, t0);
  EXPECT_FALSE(needsCancelTask(o)) << "not on a CPU yet";
  const Cpu cpus[] = {onCpu(900)};
  applyCpus(o, cpus, t0 + 2s);
  EXPECT_TRUE(needsCancelTask(o));
  EXPECT_TRUE(applyResult(o, AgentStatus::kCanceled, "", t0 + 4s).has_value());
  EXPECT_FALSE(needsCancelTask(o));
}

}  // namespace
