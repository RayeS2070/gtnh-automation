#include "domain/order.hpp"

#include <algorithm>
#include <format>

namespace factoryctl::domain {

namespace {

auto toStatus(AgentStatus status) -> OrderStatus {
  switch (status) {
    case AgentStatus::kComputing:
      return OrderStatus::kComputing;
    case AgentStatus::kAccepted:
      return OrderStatus::kAccepted;
    case AgentStatus::kFailed:
      return OrderStatus::kFailed;
    case AgentStatus::kCanceled:
      return OrderStatus::kCanceled;
    case AgentStatus::kDone:
      return OrderStatus::kDone;
  }
  return OrderStatus::kFailed;
}

// Which agent results may follow which status. Only forward: the agent
// reports on change, so a step back is a stale or confused report.
auto allowed(OrderStatus from, OrderStatus to) -> bool {
  switch (from) {
    case OrderStatus::kSent:
      return true;
    case OrderStatus::kComputing:
      return to != OrderStatus::kComputing;
    case OrderStatus::kAccepted:
    case OrderStatus::kCrafting:
      // No late failure: hasFailed() is unreliable once the craft runs.
      return to == OrderStatus::kDone || to == OrderStatus::kCanceled;
    default:
      return false;
  }
}

auto moveTo(OrderState& order, OrderStatus status, TimePoint now) -> void {
  order.status = status;
  order.changedAt = now;
  order.alarmRaised = false;
}

}  // namespace

auto markSent(OrderState& order, TimePoint now) -> bool {
  if (order.status != OrderStatus::kQueued) return false;
  moveTo(order, OrderStatus::kSent, now);
  return true;
}

auto applyResult(OrderState& order, AgentStatus result,
                 std::string_view message, TimePoint now)
    -> std::expected<void, std::string> {
  const auto to = toStatus(result);
  if (to == order.status && isOpen(to)) return {};
  if (!allowed(order.status, to)) {
    return std::unexpected(std::format("order {}: result {} after status {}",
                                       order.id, static_cast<int>(to),
                                       static_cast<int>(order.status)));
  }
  moveTo(order, to, now);
  if (to == OrderStatus::kFailed) order.message = message;
  return {};
}

auto applyCpus(OrderState& order, std::span<const Cpu> cpus, TimePoint now)
    -> void {
  const auto s = order.status;
  if (s != OrderStatus::kSent && s != OrderStatus::kComputing &&
      s != OrderStatus::kAccepted && s != OrderStatus::kCrafting) {
    return;
  }
  const auto cpu = std::ranges::find_if(
      cpus, [&](const Cpu& c) { return c.crafting == order.item; });
  if (cpu != cpus.end()) {
    if (s != OrderStatus::kCrafting) {
      moveTo(order, OrderStatus::kCrafting, now);
      order.progressAt = now;
    } else if (cpu->remaining != order.remaining) {
      order.progressAt = now;
      order.alarmRaised = false;
    }
    order.cpu = cpu->name;
    order.remaining = cpu->remaining;
    return;
  }
  // Not on a CPU: a started craft has ended; a sent or computing one may
  // still be on its way.
  if (s == OrderStatus::kAccepted || s == OrderStatus::kCrafting) {
    moveTo(order,
           order.cancelRequested ? OrderStatus::kCanceled : OrderStatus::kDone,
           now);
  }
}

auto checkTimeouts(OrderState& order, const Timeouts& limits, TimePoint now)
    -> std::optional<AlarmKind> {
  switch (order.status) {
    case OrderStatus::kSent:
      if (now - order.changedAt > limits.sent) {
        moveTo(order, OrderStatus::kLost, now);
        return AlarmKind::kLost;
      }
      break;
    case OrderStatus::kComputing:
      if (!order.alarmRaised && now - order.changedAt > limits.computing) {
        order.alarmRaised = true;
        return AlarmKind::kComputingTooLong;
      }
      break;
    case OrderStatus::kCrafting:
      if (!order.alarmRaised && now - order.progressAt > limits.stalled) {
        order.alarmRaised = true;
        return AlarmKind::kStalled;
      }
      break;
    default:
      break;
  }
  return std::nullopt;
}

auto requestCancel(OrderState& order, TimePoint now) -> CancelAction {
  if (order.status == OrderStatus::kQueued) {
    moveTo(order, OrderStatus::kCanceled, now);
    return CancelAction::kCanceledNow;
  }
  if (!isOpen(order.status) || order.cancelRequested) {
    return CancelAction::kNone;
  }
  order.cancelRequested = true;
  return CancelAction::kAskAgent;
}

auto needsCancelTask(const OrderState& order) -> bool {
  return order.cancelRequested && order.status == OrderStatus::kCrafting &&
         !order.cpu.empty();
}

auto describe(const OrderState& order) -> std::string {
  switch (order.status) {
    case OrderStatus::kQueued: return "queued";
    case OrderStatus::kSent: return "sent to the agent";
    case OrderStatus::kComputing: return "AE computes the crafting plan";
    case OrderStatus::kAccepted: return "accepted";
    case OrderStatus::kCrafting:
      return order.cpu.empty() ? "crafting" : "crafting on " + order.cpu;
    case OrderStatus::kDone: return "done";
    case OrderStatus::kFailed:
      return order.message.empty() ? "failed" : "failed: " + order.message;
    case OrderStatus::kCanceled: return "canceled";
    case OrderStatus::kLost: return "lost: no result from the agent in time";
  }
  return "?";
}

}  // namespace factoryctl::domain
