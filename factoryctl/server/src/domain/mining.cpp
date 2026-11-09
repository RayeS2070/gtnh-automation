#include "domain/mining.hpp"

#include <algorithm>
#include <cmath>
#include <set>
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

struct Choice {
  MiningSetting setting;
  std::map<ItemKey, double> rates;  // all parallels
  double value = 0;
};

auto rateOf(const std::map<ItemKey, double>& rates, const MiningNeed& need)
    -> double {
  double r = 0;
  for (const ItemKey& ore : need.ores) {
    if (const auto it = rates.find(ore); it != rates.end()) r += it->second;
  }
  return r;
}

auto evaluate(const MiningCatalog& catalog, int moduleTier, const MiningSetting& s)
    -> Choice {
  auto rates = miningRates(catalog, moduleTier, s.droneTier, s.distance,
                           s.plasmaTier);
  for (auto& [item, rate] : rates) rate *= s.parallel;
  return {.setting = s, .rates = std::move(rates)};
}

// Every setting a module of the tier can take with the drones there are.
auto settings(const MiningCatalog& catalog, int moduleTier,
              const std::map<int, int>& drones, int plasmaTier) -> std::vector<Choice> {
  std::vector<Choice> out;
  for (const auto& [tier, count] : drones) {
    if (count <= 0) continue;
    for (const int d : breakpoints(catalog, tier)) {
      auto c = evaluate(catalog, moduleTier,
                        {.droneTier = tier,
                         .distance = d,
                         .plasmaTier = plasmaTier,
                         .parallel = miningParallels(moduleTier)});
      if (!c.rates.empty()) out.push_back(std::move(c));
    }
  }
  return out;
}

// The best rate of each need's ores among the settings: what "all of it" is.
auto bests(std::span<const Choice> all, std::span<const MiningNeed> needs)
    -> std::vector<double> {
  std::vector<double> out(needs.size(), 0.0);
  for (const Choice& c : all) {
    for (std::size_t i = 0; i < needs.size(); ++i) {
      out[i] = std::max(out[i], rateOf(c.rates, needs[i]));
    }
  }
  return out;
}

// Σ weight × share of each need; the shares go to `shares` when given.
auto worth(const Choice& c, std::span<const MiningNeed> needs,
           std::span<const double> best, std::vector<double>* shares = nullptr)
    -> double {
  double v = 0;
  if (shares) shares->assign(needs.size(), 0.0);
  for (std::size_t i = 0; i < needs.size(); ++i) {
    if (best[i] <= 0) continue;
    const double share = std::min(rateOf(c.rates, needs[i]) / best[i], 1.0);
    v += needs[i].weight * share;
    if (shares) (*shares)[i] = share;
  }
  return v;
}

// What a module covers is wanted less by the next one: each need loses the
// share of it the module covers. A need's part of the module's value would
// not do: a need covered in full, but next to others in a mix, kept most of
// its weight, and the next modules took the same mix.
auto discount(std::vector<MiningNeed>& needs, std::span<const double> shares)
    -> void {
  for (std::size_t i = 0; i < needs.size(); ++i) {
    needs[i].weight *= 1.0 - std::min(shares[i], 1.0);
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
    -> std::vector<MiningNeed> {
  std::vector<MiningNeed> out;
  for (const MiningTarget& t : targets) {
    if (!t.enabled || t.threshold <= 0 || t.priority <= 0) continue;
    const auto it = stock.find(t.product);
    const double have = it == stock.end() ? 0 : it->second;
    const double fill = std::clamp(have / t.threshold, 0.0, 1.0);
    if (fill >= 1) continue;
    MiningNeed need{.product = t.product, .weight = t.priority * (1.0 - fill)};
    for (const ItemKey& ore : t.ores) {
      const auto b = buffer.find(ore);
      if (b != buffer.end() && bufferLimit > 0 && b->second >= bufferLimit) {
        continue;
      }
      need.ores.push_back(ore);
    }
    if (!need.ores.empty()) out.push_back(std::move(need));
  }
  return out;
}

auto miningWhitelist(std::span<const MiningTarget> targets,
                     const std::map<ItemKey, double>& stock, int slots)
    -> std::vector<ItemKey> {
  std::map<ItemKey, double> weight;  // an ore: its most wanting need
  for (const auto& need : miningDemand(targets, stock, {}, 0)) {
    for (const auto& ore : need.ores) {
      weight[ore] = std::max(weight[ore], need.weight);
    }
  }
  std::vector<std::pair<double, ItemKey>> ranked;
  ranked.reserve(weight.size());
  for (const auto& [ore, w] : weight) ranked.emplace_back(w, ore);
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

auto miningDrillParts(const MiningCatalog& catalog,
                      const std::map<int, int>& drones) -> std::vector<MiningItem> {
  std::vector<MiningItem> out;
  std::set<ItemKey> seen;
  for (const auto& [tier, count] : drones) {
    if (count <= 0 || tier < 0 ||
        std::cmp_greater_equal(tier, catalog.drones.size())) {
      continue;
    }
    const auto& d = catalog.drones[static_cast<std::size_t>(tier)];
    for (const MiningItem* part : {&d.tip, &d.rod}) {
      if (seen.insert(part->key).second) out.push_back(*part);
    }
  }
  return out;
}

auto miningPlasmaItem(const MiningCatalog& catalog, std::string_view fluid)
    -> std::optional<MiningItem> {
  for (const MiningPlasma& p : catalog.plasmas) {
    if (p.fluid == fluid) {
      return MiningItem{.key = {.name = "ae2fc:fluid_drop", .damage = 0, .variant = p.fluid},
                        .label = p.label};
    }
  }
  return std::nullopt;
}

auto miningUsableDrones(const MiningCatalog& catalog,
                        const std::map<int, int>& drones,
                        const std::map<ItemKey, MiningStock>& stock) -> MiningUsable {
  constexpr double kPerParallel = 4;
  const auto usable = [&](const MiningItem& part) {
    const auto it = stock.find(part.key);
    return it != stock.end() &&
           (it->second.amount >= kPerParallel || it->second.craftable);
  };
  MiningUsable out;
  std::set<ItemKey> told;
  for (const auto& [tier, count] : drones) {
    if (tier < 0 || std::cmp_greater_equal(tier, catalog.drones.size())) {
      out.drones[tier] = count;  // not in the catalog: the plan has no recipe
      continue;
    }
    const auto& d = catalog.drones[static_cast<std::size_t>(tier)];
    bool ok = true;
    for (const MiningItem* part : {&d.tip, &d.rod}) {
      if (usable(*part)) continue;
      ok = false;
      if (count > 0 && told.insert(part->key).second) out.missing.push_back(*part);
    }
    if (ok) out.drones[tier] = count;
  }
  return out;
}

auto planMining(const MiningCatalog& catalog, const MiningPlanInput& input)
    -> std::vector<MiningAssignment> {
  auto needs = input.needs;
  auto drones = input.drones;
  std::set<ItemKey> wanted;
  for (const auto& n : input.needs) wanted.insert(n.ores.begin(), n.ores.end());

  // The other setups' modules first: what they cover is covered. Their best is
  // taken over every drone tier (their pools are theirs).
  std::map<int, int> anyDrone;
  for (int t = 0; t < core::kDroneTierCount; ++t) anyDrone[t] = 1;
  for (const auto& [tier, s] : input.others) {
    const auto all = settings(catalog, tier, anyDrone, s.plasmaTier);
    const auto best = bests(all, needs);
    std::vector<double> shares;
    const auto c = evaluate(catalog, tier, s);
    worth(c, needs, best, &shares);
    discount(needs, shares);
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
    if (input.plasmaTier > 0 && !needs.empty()) {
      auto all = settings(catalog, m->tier, drones, input.plasmaTier);
      std::vector<double> best;
      Choice kept;
      const Choice* choice = nullptr;
      // Nothing left this module can cover: everything it could is covered.
      // Better twice than idle while stock is short: a new round, from the
      // whole demand.
      for (int round = 0; round < 2 && !choice; ++round) {
        if (round == 1) needs = input.needs;
        best = bests(all, needs);
        for (auto& c : all) {
          c.value = worth(c, needs, best);
          if (c.value > 0 && (!choice || c.value > choice->value)) choice = &c;
        }
        if (m->current && m->current->plasmaTier == input.plasmaTier &&
            drones[m->current->droneTier] > 0) {
          kept = evaluate(catalog, m->tier, *m->current);
          kept.value = worth(kept, needs, best);
          if (kept.value > 0 &&
              (!choice || kept.value * input.stickiness >= choice->value)) {
            choice = &kept;
          }
        }
      }
      if (choice) {
        drones[choice->setting.droneTier] -= 1;
        std::vector<double> shares;
        worth(*choice, needs, best, &shares);
        a.setting = choice->setting;
        a.value = choice->value;
        for (const auto& [item, rate] : choice->rates) {
          if (wanted.contains(item)) a.rates[item] = rate;
        }
        discount(needs, shares);
      }
    }
    out.push_back(std::move(a));
  }
  return out;
}

}  // namespace factoryctl::domain
