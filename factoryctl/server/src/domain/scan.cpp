#include "domain/scan.hpp"

#include <map>
#include <string_view>

namespace factoryctl::domain {

namespace {

constexpr std::string_view kFluidDrop = "ae2fc:fluid_drop";

}  // namespace

auto keyOf(const ScannedStack& stack) -> ItemKey {
  if (stack.name != kFluidDrop) return {stack.name, stack.damage, ""};
  return {stack.name, stack.damage,
          stack.fluid.empty() ? stack.label : stack.fluid};
}

auto mergeBatch(std::span<const ScannedStack> stacks)
    -> std::vector<ScannedGoods> {
  std::vector<ScannedGoods> out;
  std::map<ItemKey, std::size_t> index;  // key -> position in `out`
  for (const auto& s : stacks) {
    auto key = keyOf(s);
    const auto [it, added] = index.try_emplace(key, out.size());
    if (added) {
      out.push_back({std::move(key), s.label, s.size});
    } else {
      out[it->second].size += s.size;
    }
  }
  return out;
}

}  // namespace factoryctl::domain
