#include "domain/plan.hpp"

#include <algorithm>
#include <ranges>

namespace factoryctl::domain {

namespace {

auto isTarget(std::span<const Target> targets, const ItemKey& item) -> bool {
  return std::ranges::contains(targets, item, &Target::item);
}

// Low priority may use the CPUs only when no CPU does foreign work. A busy
// CPU without a visible output counts as foreign: we cannot tell. An output
// counts even without `busy` (see Cpu).
auto cpusFreeForLow(const PlanInput& in) -> bool {
  return std::ranges::none_of(in.cpus, [&](const Cpu& cpu) {
    if (cpu.crafting) return !isTarget(in.targets, *cpu.crafting);
    return cpu.busy;
  });
}

auto inProgress(const PlanInput& in, const ItemKey& item) -> bool {
  const auto ordered = std::ranges::any_of(in.orders, [&](const Order& o) {
    return o.item == item && isOpen(o.status);
  });
  const auto crafted = std::ranges::any_of(
      in.cpus, [&](const Cpu& cpu) { return cpu.crafting == item; });
  return ordered || crafted;
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
  const bool lowAllowed = cpusFreeForLow(in);

  for (const auto& t : in.targets) {
    if (t.batch <= 0) continue;
    if (t.priority == Priority::kLow && !lowAllowed) continue;
    if (inProgress(in, t.item)) continue;
    const bool always = t.threshold <= 0;
    if (always || stockOf(*in.stock, t.item) < t.threshold) {
      out.push_back({t.item, t.batch});
    }
  }
  return out;
}

}  // namespace factoryctl::domain
