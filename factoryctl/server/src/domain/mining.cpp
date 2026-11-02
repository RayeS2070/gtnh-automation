#include "domain/mining.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/mechanics.hpp"

namespace factoryctl::domain {

namespace {

constexpr int kMaxDistance = 300;  // TileEntityModuleMiner.MAX_DISTANCE
constexpr double kOverdrive = 1.0;
constexpr int kTicksPerSecond = 20;

// The distances where the candidates change for a drone tier: every range
// start and the place after every range end. Between two of them the rates
// are the same, so the first distance of each interval stands for it.
auto breakpoints(const MiningCatalog& catalog, int droneTier)
    -> std::vector<int> {
  std::vector<int> out{1};
  for (const core::Recipe& r : catalog.recipes) {
    if (r.droneTier != droneTier) continue;
    out.push_back(r.minDistance);
    if (r.maxDistance < kMaxDistance) out.push_back(r.maxDistance + 1);
  }
  std::ranges::sort(out);
  const auto [first, last] = std::ranges::unique(out);
  out.erase(first, last);
  std::erase_if(out, [](int d) { return d < 1 || d > kMaxDistance; });
  return out;
}

auto worth(const std::map<ItemKey, double>& rates,
           const std::map<ItemKey, double>& demand) -> double {
  double v = 0;
  for (const auto& [item, rate] : rates) {
    if (const auto it = demand.find(item); it != demand.end()) {
      v += it->second * rate;
    }
  }
  return v;
}

struct Choice {
  MiningSetting setting;
  double value = 0;
  std::map<ItemKey, double> rates;  // all parallels
};

auto evaluate(const MiningCatalog& catalog, int moduleTier,
              const MiningSetting& s, const std::map<ItemKey, double>& demand)
    -> Choice {
  auto rates = miningRates(catalog, moduleTier, s.droneTier, s.distance,
                           s.plasmaTier);
  for (auto& [item, rate] : rates) rate *= s.parallel;
  return {.setting = s, .value = worth(rates, demand), .rates = std::move(rates)};
}

auto best(const MiningCatalog& catalog, int moduleTier,
          const std::map<int, int>& drones, int plasmaTier,
          const std::map<ItemKey, double>& demand) -> std::optional<Choice> {
  std::optional<Choice> out;
  for (const auto& [tier, count] : drones) {
    if (count <= 0) continue;
    for (const int d : breakpoints(catalog, tier)) {
      auto c = evaluate(catalog, moduleTier,
                        {.droneTier = tier,
                         .distance = d,
                         .plasmaTier = plasmaTier,
                         .parallel = miningParallels(moduleTier)},
                        demand);
      if (c.value > 0 && (!out || c.value > out->value)) out = std::move(c);
    }
  }
  return out;
}

// What a module covers is wanted less by the next one: each ore loses half of
// its share of the module's value.
auto discount(std::map<ItemKey, double>& demand, const Choice& c) -> void {
  if (c.value <= 0) return;
  for (auto& [item, weight] : demand) {
    const auto it = c.rates.find(item);
    if (it == c.rates.end()) continue;
    const double share = weight * it->second / c.value;
    weight *= 1.0 - 0.5 * std::min(share, 1.0);
  }
}

}  // namespace

auto miningParallels(int moduleTier) -> int {
  switch (moduleTier) {
    case 1:
      return 2;
    case 2:
      return 4;
    default:
      return 8;
  }
}

auto miningRates(const MiningCatalog& catalog, int moduleTier, int droneTier,
                 int distance, int plasmaTier) -> std::map<ItemKey, double> {
  const core::MinerModule miner{.tier = moduleTier};
  core::Settings settings{.distance = distance, .plasmaTier = plasmaTier,
                          .overdrive = kOverdrive};
  settings.drones.set(static_cast<std::size_t>(droneTier));
  const int bonus = core::bonusChance(plasmaTier, kOverdrive);

  double weightedTicks = 0;
  std::map<ItemKey, double> items;  // Σ weight · expected items
  for (const core::Recipe& r : catalog.recipes) {
    if (!core::isCandidate(r, miner, settings)) continue;
    weightedTicks += static_cast<double>(r.weight) *
                     core::recipeTime(r.duration, plasmaTier, kOverdrive);
    const core::Asteroid& a = catalog.smc.asteroids[r.asteroid];
    const auto shares = core::outputShares(a.outputs);
    const double draws = core::expectedDraws(r, bonus);
    for (std::size_t j = 0; j < a.outputs.size(); ++j) {
      items[catalog.outputs[r.asteroid][j].item.key] +=
          r.weight * draws * shares[j] * a.outputs[j].amount;
    }
  }
  if (weightedTicks <= 0) return {};
  for (auto& [item, v] : items) v *= kTicksPerSecond / weightedTicks;
  return items;
}

auto miningDemand(std::span<const MiningTarget> targets,
                  const std::map<ItemKey, double>& stock,
                  const std::map<ItemKey, double>& buffer, double bufferLimit)
    -> std::map<ItemKey, double> {
  std::map<ItemKey, double> out;
  for (const MiningTarget& t : targets) {
    if (!t.enabled || t.threshold <= 0 || t.priority <= 0) continue;
    const auto it = stock.find(t.product);
    const double have = it == stock.end() ? 0 : it->second;
    const double fill = std::clamp(have / t.threshold, 0.0, 1.0);
    const double urgency = 1.0 - std::pow(fill, t.priority);
    if (urgency <= 0) continue;
    for (const ItemKey& ore : t.ores) {
      const auto b = buffer.find(ore);
      if (b != buffer.end() && bufferLimit > 0 && b->second >= bufferLimit) {
        continue;
      }
      out[ore] += urgency;
    }
  }
  return out;
}

auto miningWhitelist(std::span<const MiningTarget> targets,
                     const std::map<ItemKey, double>& stock, int slots)
    -> std::vector<ItemKey> {
  const auto demand = miningDemand(targets, stock, {}, 0);
  std::vector<std::pair<double, ItemKey>> ranked;
  for (const auto& [ore, w] : demand) ranked.emplace_back(w, ore);
  std::ranges::sort(ranked, [](const auto& a, const auto& b) {
    if (a.first != b.first) return a.first > b.first;
    return a.second < b.second;
  });
  std::vector<ItemKey> out;
  for (const auto& [w, ore] : ranked) {
    if (std::cmp_greater_equal(out.size(), std::max(slots, 0))) break;
    out.push_back(ore);
  }
  return out;
}

auto bestPlasma(const MiningCatalog& catalog,
                const std::map<std::string, double>& stock, double minimum)
    -> int {
  int out = 0;
  for (const MiningPlasma& p : catalog.plasmas) {
    const auto it = stock.find(p.fluid);
    if (it != stock.end() && it->second >= minimum) out = std::max(out, p.tier);
  }
  return out;
}

auto planMining(const MiningCatalog& catalog, const MiningPlanInput& input)
    -> std::vector<MiningAssignment> {
  auto demand = input.demand;
  auto drones = input.drones;
  for (const auto& [tier, s] : input.others) {
    discount(demand, evaluate(catalog, tier, s, demand));
  }

  // The bigger modules first: they get the drones that are worth the most.
  std::vector<const MiningModule*> order;
  for (const MiningModule& m : input.modules) order.push_back(&m);
  std::ranges::stable_sort(order, [](const auto* a, const auto* b) {
    return a->tier > b->tier;
  });

  std::vector<MiningAssignment> out;
  for (const MiningModule* m : order) {
    MiningAssignment a{.module = m->id};
    if (input.plasmaTier > 0) {
      auto choice = best(catalog, m->tier, drones, input.plasmaTier, demand);
      if (m->current && m->current->plasmaTier == input.plasmaTier &&
          drones[m->current->droneTier] > 0) {
        auto kept = evaluate(catalog, m->tier, *m->current, demand);
        if (kept.value > 0 &&
            (!choice || kept.value * input.stickiness >= choice->value)) {
          choice = std::move(kept);
        }
      }
      if (choice) {
        drones[choice->setting.droneTier] -= 1;
        discount(demand, *choice);
        a.setting = choice->setting;
        a.value = choice->value;
        for (const auto& [item, rate] : choice->rates) {
          if (input.demand.contains(item)) a.rates[item] = rate;
        }
      }
    }
    out.push_back(std::move(a));
  }
  return out;
}

}  // namespace factoryctl::domain
