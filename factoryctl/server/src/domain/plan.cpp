#include "domain/plan.hpp"

#include <algorithm>
#include <ranges>

namespace factoryctl::domain {

namespace {

// Idle: not busy and showing no output (an output with busy=false is a craft
// that has just started). An empty report has no idle CPUs.
auto idleCpus(const PlanInput& in) -> std::int64_t {
  return std::ranges::count_if(
      in.cpus, [](const Cpu& cpu) { return !cpu.busy && !cpu.crafting; });
}

auto inProgress(const PlanInput& in, const ItemKey& item) -> bool {
  const auto ordered = std::ranges::any_of(in.orders, [&](const Order& o) {
    return o.item == item && isOpen(o.status);
  });
  const auto crafted = std::ranges::any_of(
      in.cpus, [&](const Cpu& cpu) { return cpu.crafting == item; });
  return ordered || crafted;
}

// What is missing, rounded up to whole batches.
auto fillAmount(std::int64_t missing, std::int64_t batch) -> std::int64_t {
  return (missing + batch - 1) / batch * batch;
}

auto stockOf(const Stock& stock, const ItemKey& item) -> std::int64_t {
  const auto it = stock.find(item);
  return it == stock.end() ? 0 : it->second;
}

}  // namespace

auto isOpen(OrderStatus status) -> bool {
  switch (status) {
    case OrderStatus::kQueued:
    case OrderStatus::kSent:
    case OrderStatus::kComputing:
    case OrderStatus::kAccepted:
    case OrderStatus::kCrafting:
      return true;
    case OrderStatus::kDone:
    case OrderStatus::kFailed:
    case OrderStatus::kCanceled:
    case OrderStatus::kLost:
      return false;
  }
  return false;
}

auto plan(const PlanInput& in) -> std::vector<NewOrder> {
  std::vector<NewOrder> out;
  if (in.stock == nullptr) return out;
  const bool lowAllowed = idleCpus(in) >= in.lowFreeCpus;

  for (const auto& t : in.targets) {
    if (t.batch <= 0) continue;
    if (t.priority == Priority::kLow && !lowAllowed) continue;
    if (inProgress(in, t.item)) continue;
    if (std::ranges::contains(in.settling, t.item)) continue;
    const bool always = t.threshold <= 0;
    const auto have = stockOf(*in.stock, t.item);
    if (always || have < t.threshold) {
      out.push_back({t.item, always || !t.fill
                                 ? t.batch
                                 : fillAmount(t.threshold - have, t.batch)});
    }
  }
  return out;
}

}  // namespace factoryctl::domain
