#pragma once

// The life of one order on the server: what the agent reports moves it on,
// time limits turn silence into `lost` or an alarm. Plain C++, no I/O.

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "domain/plan.hpp"

namespace factoryctl::domain {

using TimePoint = std::chrono::system_clock::time_point;

struct OrderState {
  std::int64_t id = 0;
  ItemKey item;
  std::string label;  // as the game shows it; the agent finds drops by it
  std::int64_t amount = 0;
  OrderStatus status = OrderStatus::kQueued;
  TimePoint changedAt;  // last change of `status`
  std::string message;  // from the agent, for failed orders

  std::string cpu;             // the CPU that crafts it, once seen
  std::int64_t remaining = 0;  // its finalOutput().size
  TimePoint progressAt;        // last change of `remaining`

  bool cancelRequested = false;
  bool alarmRaised = false;  // the current stall or long computing is logged
};

// What the agent says about an order it was given.
enum class AgentStatus {
  kComputing,
  kAccepted,
  kFailed,  // includes "not craftable" and "ambiguous filter"
  kCanceled,
  kDone,
};

struct Timeouts {
  std::chrono::seconds sent{120};       // no result: lost
  std::chrono::seconds computing{120};  // alarm
  std::chrono::seconds stalled{600};    // remaining unchanged: alarm

  auto operator==(const Timeouts&) const -> bool = default;
};

enum class AlarmKind {
  kLost,
  kComputingTooLong,
  kStalled,
  kNoLongerCraftable,  // a craft on a CPU whose pattern is gone: may hang
};

// The order was put into a poll response. Only a queued order can be sent:
// each order goes to the agent at most once.
auto markSent(OrderState& order, TimePoint now) -> bool;

// A result from the agent. Fails, leaving the order as it is, when the
// result does not fit the status (a result for a queued or closed order, a
// late `failed` for a craft already on a CPU). The same status again is fine.
auto applyResult(OrderState& order, AgentStatus status,
                 std::string_view message, TimePoint now)
    -> std::expected<void, std::string>;

// The CPUs of the last poll. An order in progress whose item a CPU crafts
// becomes `crafting`; a crafting or accepted order no CPU shows any more has
// finished: `done`, or `canceled` if a cancel was asked for.
auto applyCpus(OrderState& order, std::span<const Cpu> cpus, TimePoint now)
    -> void;

// Time limits. Returns an alarm to log, once per stall or long computing;
// a sent order without a result becomes lost.
auto checkTimeouts(OrderState& order, const Timeouts& limits, TimePoint now)
    -> std::optional<AlarmKind>;

enum class CancelAction {
  kNone,         // closed already, or asked before
  kCanceledNow,  // queued: never reached the agent
  kAskAgent,     // in the agent's hands: send a cancel task when on a CPU
};

auto requestCancel(OrderState& order, TimePoint now) -> CancelAction;

// The order's status for a person: "crafting on CPU_014", "failed: <why>".
auto describe(const OrderState& order) -> std::string;

// Whether the next poll response carries a cancel task for this order: the
// agent cancels by CPU name, so the craft must be on a CPU.
auto needsCancelTask(const OrderState& order) -> bool;

}  // namespace factoryctl::domain
