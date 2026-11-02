#pragma once

// Space Elevator mining: what each module should mine. The server plans, the
// agent (factoryctl/mining) carries it out. The module draws its asteroid at
// random among the candidates its drone and distance allow, so a setting is a
// mix of outputs: it is worth what its outputs are worth to the targets.
// The mechanics are smc's (../smc/core); the planning on top is a greedy
// placeholder, smc is where a better one belongs.

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/model.hpp"
#include "domain/plan.hpp"

namespace factoryctl::domain {

struct MiningItem {
  ItemKey key;
  std::string label;
};

struct MiningOutput {
  MiningItem item;                   // the ore (or dust) the module puts out
  std::vector<MiningItem> products;  // what processing makes of it
};

// A drone tier with the drill it takes (ItemMiningDrones.DroneMaterials).
struct MiningDrone {
  int tier = 0;  // index in core::kDroneTierNames
  std::string name;
  MiningItem drone, tip, rod;
};

struct MiningPlasma {
  int tier = 0;  // 1..core::kPlasmaTierCount
  std::string fluid;
  std::string label;
  int usage = 0;  // mB per parallel per operation
};

struct MiningCatalog {
  core::Catalog smc;
  std::vector<std::vector<MiningOutput>> outputs;  // per asteroid, as smc's
  std::vector<core::Recipe> recipes;               // core::expand(smc)
  std::vector<MiningDrone> drones;                 // by tier
  std::vector<MiningPlasma> plasmas;               // by tier, from 1
};

// What a module is set to. Overdrive is 1: OC has no parameter for it.
struct MiningSetting {
  int droneTier = 0;
  int distance = 1;
  int plasmaTier = 1;
  int parallel = 1;

  auto operator<=>(const MiningSetting&) const = default;
};

// The module's max parallels, by tier 1..3.
auto miningParallels(int moduleTier) -> int;

// Expected items per second of each output item, per parallel, for a module
// of that tier with one drone of `droneTier` at `distance`.
auto miningRates(const MiningCatalog& catalog, int moduleTier, int droneTier,
                 int distance, int plasmaTier) -> std::map<ItemKey, double>;

struct MiningTarget {
  ItemKey product;   // what is kept stocked: a dust, a fluid (keyOf)
  double threshold = 0;
  double priority = 1;  // > 0: a weight, 2 is twice as important
  bool enabled = true;
  std::vector<ItemKey> ores;  // the module outputs it is made of
};

// What a target short of stock wants now: its weight, priority × (1 - fill),
// and the ores that feed it. Ores whose buffer before processing is at
// `bufferLimit` are left out until it drains (the factory is not keeping up).
struct MiningNeed {
  ItemKey product;
  double weight = 0;
  std::vector<ItemKey> ores;
};

auto miningDemand(std::span<const MiningTarget> targets,
                  const std::map<ItemKey, double>& stock,
                  const std::map<ItemKey, double>& buffer, double bufferLimit)
    -> std::vector<MiningNeed>;

// The ores the storage bus keeps (the rest is voided): every ore of an enabled
// target short of stock, the most wanted first, at most `slots`. Blocked by
// the buffer or not: an ore on its way must not be voided.
auto miningWhitelist(std::span<const MiningTarget> targets,
                     const std::map<ItemKey, double>& stock, int slots)
    -> std::vector<ItemKey>;

// The highest plasma tier with at least `minimum` mB in stock (by fluid
// name); 0: none.
auto bestPlasma(const MiningCatalog& catalog,
                const std::map<std::string, double>& stock, double minimum)
    -> int;

struct MiningModule {
  std::string id;  // its coordinates: "x,y,z"
  int tier = 1;
  std::optional<MiningSetting> current;
};

struct MiningPlanInput {
  std::span<const MiningModule> modules;  // the ones to plan
  // Modules of other setups keep their settings: their tier and setting.
  std::span<const std::pair<int, MiningSetting>> others;
  std::map<int, int> drones;  // by tier: in the pool and in these modules
  int plasmaTier = 0;
  std::vector<MiningNeed> needs;
  // A module keeps its setting unless the best one is worth this much more:
  // a change costs a drone swap and a stop.
  double stickiness = 1.25;
};

struct MiningAssignment {
  std::string module;
  std::optional<MiningSetting> setting;  // none: stay idle
  double value = 0;
  std::map<ItemKey, double> rates;  // per second, all parallels, wanted ones
};

// One assignment per module, the higher tiers planned first. A setting is
// worth Σ need weight × the share of the need it covers: its rate of the
// need's ores over the best rate this module could have for them. So a rare
// ore counts as much as a plentiful one (items of different ores do not
// compare); the weights decide. Each module discounts what it covers for the
// next (half of its share of every need), after the other setups' modules did.
auto planMining(const MiningCatalog& catalog, const MiningPlanInput& input)
    -> std::vector<MiningAssignment>;

}  // namespace factoryctl::domain
