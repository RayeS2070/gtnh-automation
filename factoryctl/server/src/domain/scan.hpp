#pragma once

// A batch of the agent's scan, before it goes to the database.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "domain/plan.hpp"

namespace factoryctl::domain {

// One stack as getItemsInNetworkById returns it, only the fields we use.
struct ScannedStack {
  std::string name;
  std::int32_t damage = 0;
  std::string label;
  std::int64_t size = 0;
  std::string fluid;  // the fluid's name: of a drop (fluidDrop.name) or of an
                      // AE fluid (2.9, `name` then is the fluid's too)
  bool craftable = true;  // isCraftable: AE has a pattern for it
};

// What the database keeps per item: stacks of one key summed up.
struct ScannedGoods {
  ItemKey key;
  std::string label;
  std::int64_t size = 0;
  bool craftable = true;  // any of its stacks is

  auto operator==(const ScannedGoods&) const -> bool = default;
};

// A fluid is keyed by its name, the same whether it came as a drop (2.8.4) or
// as an AE fluid (2.9): {ae2fc:fluid_drop, 0, name}, see ItemKey. A drop
// without a fluid has never been seen; it falls back to its label rather than
// share a key with every other drop.
auto keyOf(const ScannedStack& stack) -> ItemKey;

// Stacks of the same key become one, sizes summed, the first label kept; the
// order of first appearance is kept too. One key comes as several stacks for
// items told apart by NBT only, and for a stored stack next to its craftable
// entry.
auto mergeBatch(std::span<const ScannedStack> stacks)
    -> std::vector<ScannedGoods>;

}  // namespace factoryctl::domain
