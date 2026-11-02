#include <chrono>
#include <userver/utest/utest.hpp>

#include "core/mechanics.hpp"
#include "domain/mining.hpp"
#include "mining/catalog.hpp"

namespace {

using namespace factoryctl::domain;

auto ore(int damage) -> ItemKey {
  return {.name = "gregtech:gt.blockores2", .damage = damage};
}

// Two LV-only T1 asteroids: A at 1..10 (weight 100), B at 5..20 (weight 300);
// both 100 ticks, one draw (min = max size 1), 1 item a draw.
auto twoAsteroids() -> MiningCatalog {
  MiningCatalog c;
  const auto asteroid = [](std::string name, int from, int to, int weight,
                           std::vector<core::Output> outputs) {
    return core::Asteroid{.name = std::move(name),
                          .outputs = std::move(outputs),
                          .minSize = 1,
                          .maxSize = 1,
                          .minDistance = from,
                          .maxDistance = to,
                          .minModuleTier = 1,
                          .duration = 100,
                          .eut = 1,
                          .weight = weight};
  };
  c.smc.asteroids = {
      asteroid("a", 1, 10, 100,
               {{.name = "X", .amount = 1, .chance = 7500},
                {.name = "Y", .amount = 1, .chance = 2500}}),
      asteroid("b", 5, 20, 300, {{.name = "Z", .amount = 1, .chance = 10000}})};
  c.outputs = {{{.item = {.key = ore(1)}}, {.item = {.key = ore(2)}}},
               {{.item = {.key = ore(3)}}}};
  c.recipes = core::expand(c.smc.asteroids);
  c.plasmas = {{.tier = 1, .fluid = "plasma.helium", .usage = 825},
               {.tier = 3, .fluid = "plasma.radon", .usage = 375}};
  return c;
}

// Plasma tier 1: no time bonus, 100 ticks; bonus draws do not happen (min =
// max), so one draw per operation.
TEST(MiningRates, OneAsteroid) {
  const auto c = twoAsteroids();
  const auto r = miningRates(c, 1, 0, 2, 1);  // only A at 2
  ASSERT_EQ(r.size(), 2);
  // 1 operation per 100 ticks = 0.2 per second; X takes 3/4 of the draws.
  EXPECT_DOUBLE_EQ(r.at(ore(1)), 0.15);
  EXPECT_DOUBLE_EQ(r.at(ore(2)), 0.05);
}

TEST(MiningRates, TwoAsteroidsByWeight) {
  const auto c = twoAsteroids();
  const auto r = miningRates(c, 1, 0, 7, 1);  // A and B: B 3 times as likely
  EXPECT_DOUBLE_EQ(r.at(ore(3)), 0.2 * 0.75);
  EXPECT_DOUBLE_EQ(r.at(ore(1)), 0.2 * 0.25 * 0.75);
}

TEST(MiningRates, NothingThere) {
  EXPECT_TRUE(miningRates(twoAsteroids(), 1, 0, 50, 1).empty());
  EXPECT_TRUE(miningRates(twoAsteroids(), 1, 3, 2, 1).empty());  // HV: none
}

auto target(ItemKey product, double threshold, std::vector<ItemKey> ores)
    -> MiningTarget {
  return {.product = std::move(product),
          .threshold = threshold,
          .ores = std::move(ores)};
}

const ItemKey kDustX{.name = "dust", .damage = 1};
const ItemKey kDustZ{.name = "dust", .damage = 3};

TEST(MiningDemand, ShortOfStockOnly) {
  const std::vector targets{target(kDustX, 100, {ore(1)}),
                            target(kDustZ, 100, {ore(3)})};
  const auto d = miningDemand(targets, {{kDustX, 50}, {kDustZ, 100}}, {}, 0);
  ASSERT_EQ(d.size(), 1);
  EXPECT_DOUBLE_EQ(d.at(ore(1)), 0.5);
}

TEST(MiningDemand, FullBufferBlocks) {
  const std::vector targets{target(kDustX, 100, {ore(1), ore(2)})};
  const auto d = miningDemand(targets, {}, {{ore(1), 5000}}, 5000);
  EXPECT_FALSE(d.contains(ore(1)));
  EXPECT_DOUBLE_EQ(d.at(ore(2)), 1);
}

TEST(MiningDemand, DisabledAndPriority) {
  auto off = target(kDustX, 100, {ore(1)});
  off.enabled = false;
  auto urgent = target(kDustZ, 100, {ore(3)});
  urgent.priority = 2;
  const std::vector targets{off, urgent};
  const auto d = miningDemand(targets, {{kDustZ, 50}}, {}, 0);
  ASSERT_EQ(d.size(), 1);
  EXPECT_DOUBLE_EQ(d.at(ore(3)), 0.75);  // 1 - 0.5^2
}

TEST(MiningWhitelist, MostWantedFirstAndCut) {
  const std::vector targets{target(kDustX, 100, {ore(1)}),
                            target(kDustZ, 100, {ore(3)})};
  // A full buffer does not take an ore out: it must not be voided.
  EXPECT_EQ(miningWhitelist(targets, {{kDustX, 90}}, 63),
            (std::vector{ore(3), ore(1)}));
  EXPECT_EQ(miningWhitelist(targets, {{kDustX, 90}}, 1), (std::vector{ore(3)}));
}

TEST(BestPlasma, HighestInStock) {
  const auto c = twoAsteroids();
  EXPECT_EQ(bestPlasma(c, {{"plasma.helium", 1e6}, {"plasma.radon", 1e6}}, 1e4),
            3);
  EXPECT_EQ(bestPlasma(c, {{"plasma.helium", 1e6}, {"plasma.radon", 10}}, 1e4),
            1);
  EXPECT_EQ(bestPlasma(c, {}, 1e4), 0);
}

auto plan(const MiningCatalog& c, std::vector<MiningModule> modules,
          std::map<ItemKey, double> demand, std::map<int, int> drones = {{0, 9}},
          std::vector<std::pair<int, MiningSetting>> others = {}) {
  return planMining(c, {.modules = modules,
                        .others = others,
                        .drones = std::move(drones),
                        .plasmaTier = 1,
                        .demand = std::move(demand)});
}

TEST(PlanMining, WantedAloneGoesWhereItIsAlone) {
  const auto c = twoAsteroids();
  // Only X is wanted: at 1..4 A is the only candidate.
  const auto p = plan(c, {{.id = "m1", .tier = 1}}, {{ore(1), 1}});
  ASSERT_EQ(p.size(), 1);
  ASSERT_TRUE(p[0].setting);
  EXPECT_EQ(p[0].setting->distance, 1);
  EXPECT_EQ(p[0].setting->parallel, 2);
  EXPECT_DOUBLE_EQ(p[0].rates.at(ore(1)), 0.3);
  EXPECT_FALSE(p[0].rates.contains(ore(2)));  // not wanted: voided
}

TEST(PlanMining, ZWantedGoesPastA) {
  const auto c = twoAsteroids();
  const auto p = plan(c, {{.id = "m1", .tier = 1}}, {{ore(3), 1}});
  ASSERT_TRUE(p[0].setting);
  EXPECT_EQ(p[0].setting->distance, 11);  // B alone at 11..20
}

TEST(PlanMining, NoDroneNoPlasmaNoDemandIdle) {
  const auto c = twoAsteroids();
  EXPECT_FALSE(plan(c, {{.id = "m1"}}, {{ore(1), 1}}, {})[0].setting);
  EXPECT_FALSE(plan(c, {{.id = "m1"}}, {})[0].setting);
  const std::vector<MiningModule> one{{.id = "m1"}};
  EXPECT_FALSE(planMining(c, {.modules = one,
                              .drones = {{0, 1}},
                              .plasmaTier = 0,
                              .demand = {{ore(1), 1}}})[0]
                   .setting);
}

TEST(PlanMining, OneDroneOneModule) {
  const auto c = twoAsteroids();
  const auto p = plan(c, {{.id = "m1"}, {.id = "m2"}}, {{ore(1), 1}}, {{0, 1}});
  EXPECT_TRUE(p[0].setting);
  EXPECT_FALSE(p[1].setting);
}

TEST(PlanMining, SecondModuleGoesForTheOtherNeed) {
  const auto c = twoAsteroids();
  // X at 1 is worth 1 · 0.3, Z at 11 0.5 · 0.4: the first module takes X;
  // the discount halves X (0.15) and turns the second one to Z (0.2).
  const auto p = plan(c, {{.id = "m1"}, {.id = "m2"}},
                      {{ore(1), 1}, {ore(3), 0.5}});
  ASSERT_TRUE(p[0].setting && p[1].setting);
  EXPECT_EQ(p[0].setting->distance, 1);
  EXPECT_EQ(p[1].setting->distance, 11);
}

TEST(PlanMining, OthersCountFirst) {
  const auto c = twoAsteroids();
  const MiningSetting x{.droneTier = 0, .distance = 1, .plasmaTier = 1,
                        .parallel = 2};
  const auto p = plan(c, {{.id = "m2"}}, {{ore(1), 1}, {ore(3), 0.5}}, {{0, 9}},
                      {{1, x}});
  EXPECT_EQ(p[0].setting->distance, 11);
}

TEST(PlanMining, KeepsItsSettingUnlessMuchBetter) {
  const auto c = twoAsteroids();
  // At 7 both asteroids: X at 0.3·0.25·0.75 = 0.05625 a second, the best (1)
  // gives 0.3; much better, so it moves.
  MiningSetting mixed{.droneTier = 0, .distance = 7, .plasmaTier = 1,
                      .parallel = 2};
  auto p = plan(c, {{.id = "m1", .current = mixed}}, {{ore(1), 1}});
  EXPECT_EQ(p[0].setting->distance, 1);
  // Distance 3 is as good as 1 (same candidates): it stays.
  mixed.distance = 3;
  p = plan(c, {{.id = "m1", .current = mixed}}, {{ore(1), 1}});
  EXPECT_EQ(p[0].setting->distance, 3);
}

TEST(MiningCatalog, Embedded) {
  const auto& c = factoryctl::mining::catalog();
  EXPECT_EQ(c.smc.gt5uVersion, "5.09.54.264");
  EXPECT_EQ(c.smc.asteroids.size(), 43);
  EXPECT_EQ(c.drones.size(), 13);
  EXPECT_EQ(c.drones[9].tip.key, (ItemKey{.name = "gregtech:gt.metaitem.02",
                                          .damage = 8129}));
  EXPECT_EQ(c.plasmas.size(), 5);
  // Cosmic Neutronium ore, as the storage bus showed it in game: a UEV drone
  // in a T2 module finds it somewhere.
  bool any = false;
  for (int d = 1; d <= 300 && !any; ++d) {
    any = miningRates(c, 2, 9, d, 3).contains(ore(2982));
  }
  EXPECT_TRUE(any);
}

// The plan runs in every poll: a dozen modules with every drone must be quick.
TEST(MiningCatalog, PlanIsQuick) {
  const auto& c = factoryctl::mining::catalog();
  std::map<ItemKey, double> demand;
  for (std::size_t a = 0; a < c.outputs.size(); a += 4) {
    demand[c.outputs[a][0].item.key] = 1;
  }
  std::map<int, int> drones;
  for (const auto& d : c.drones) drones[d.tier] = 2;
  std::vector<MiningModule> modules;
  for (int i = 0; i < 12; ++i) {
    modules.push_back({.id = std::to_string(i), .tier = 3});
  }
  const auto start = std::chrono::steady_clock::now();
  const auto p = planMining(c, {.modules = modules,
                                .drones = drones,
                                .plasmaTier = 5,
                                .demand = demand});
  const auto took = std::chrono::steady_clock::now() - start;
  int busy = 0;
  for (const auto& a : p) busy += a.setting ? 1 : 0;
  EXPECT_EQ(busy, 12);
  EXPECT_LT(took, std::chrono::seconds(1));
}

}  // namespace
