#pragma once

// Domain logic: plain C++, no userver and no I/O, so it is tested alone.

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace factoryctl::domain {

// An item as the ME network knows it. Fluid drops (ae2fc:fluid_drop) all have
// damage 0 and differ in NBT only, which OC does not show: their `variant` is
// the fluid's internal name (fluidDrop.name, e.g. molten.platinum); for other
// items it stays empty.
struct ItemKey {
  std::string name;
  std::int32_t damage = 0;
  std::string variant;

  auto operator<=>(const ItemKey&) const = default;
};

enum class Priority { kHigh, kLow };

// Keep `item` stocked: order `batch` when the stock is below `threshold`.
// A threshold of 0 orders whenever nothing is on order (passive production).
struct Target {
  ItemKey item;
  std::int64_t threshold = 0;
  std::int64_t batch = 0;
  Priority priority = Priority::kHigh;
};

enum class OrderStatus {
  kQueued,     // made by the server, not given to the agent yet
  kSent,       // given to the agent once, no result yet
  kComputing,  // AE is computing the crafting plan
  kAccepted,   // AE accepted the request
  kCrafting,   // a CPU crafts it
  kDone,
  kFailed,    // AE refused it, or nothing matched the filter
  kCanceled,  // canceled by a user
  kLost,      // sent, but no result came in time; never resent by itself
};

auto isOpen(OrderStatus status) -> bool;

struct Order {
  ItemKey item;
  OrderStatus status = OrderStatus::kQueued;
};

// A crafting CPU as the agent reports it. Its final output needs a crafting
// monitor; `remaining` is finalOutput().size, what is left to deliver: it
// only goes down while the craft runs (checked in game). The output, not
// `busy`, says what the CPU crafts: in game getCpus() once said busy=false
// for a CPU whose finalOutput() already showed a craft that had just started.
struct Cpu {
  bool busy = false;
  std::optional<ItemKey> crafting;
  std::string name;
  std::int64_t remaining = 0;
};

// Stock from the last complete scan: an item that is absent has 0.
using Stock = std::map<ItemKey, std::int64_t>;

struct PlanInput {
  std::span<const Target> targets;
  const Stock* stock = nullptr;  // nullptr: no complete scan yet
  std::span<const Order> orders;
  std::span<const Cpu> cpus;
  // Low priority runs only while at least this many CPUs are idle: it takes
  // idle CPUs and leaves a reserve for everything else.
  std::int32_t lowFreeCpus = 0;
  // Goods whose order closed after the stock was measured: the stock does not
  // show what was made yet, so they wait for the next scan.
  std::span<const ItemKey> settling;
};

struct NewOrder {
  ItemKey item;
  std::int64_t amount = 0;

  auto operator==(const NewOrder&) const -> bool = default;
};

// Orders to place now, in the order of `targets`.
auto plan(const PlanInput& input) -> std::vector<NewOrder>;

}  // namespace factoryctl::domain
