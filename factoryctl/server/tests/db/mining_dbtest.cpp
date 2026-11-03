#include <userver/formats/json/serialize.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/events.hpp"
#include "db/mining.hpp"
#include "db/scan.hpp"
#include "db/schema.hpp"
#include "db/targets.hpp"
#include "fixture.hpp"

namespace {

using namespace factoryctl::domain;
using namespace factoryctl::db;
using factoryctl::test::DbTest;
namespace json = userver::formats::json;

const TimePoint t0{std::chrono::sys_days{std::chrono::October / 30 / 2026}};

class MiningTest : public DbTest {
 protected:
  MiningTest() {
    (void)ensureSchema(db());
    const auto id = startScan(db(), t0);
    const std::vector<ScannedGoods> goods{
        {{"gregtech:gt.metaitem.01", 2982, ""}, "Cosmic Neutronium Dust", 5000},
        {{"ae2fc:fluid_drop", 0, "plasma.radon"}, "drop of Radon Plasma", 2'000'000}};
    (void)storeBatch(db(), id, goods);
    (void)finishScan(db(), id, t0);
    dust = findStock(db(), {.text = "cosmic", .limit = 1}).at(0).goodsId;
  }

  auto tx() { return db().Begin(userver::storages::postgres::ClusterHostType::kMaster, {}); }

  std::int64_t dust = 0;
};

const ItemKey kOre{.name = "gregtech:gt.blockores2", .damage = 2982};

UTEST_F(MiningTest, TargetWithItsStock) {
  ASSERT_TRUE(setMiningTarget(db(), dust,
                              {.threshold = 10000, .priority = 2, .ores = {kOre}},
                              t0, "pavel")
                  .has_value());
  const auto list = listMiningTargets(db());
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list[0].goods.amount, 5000);
  EXPECT_EQ(list[0].target.threshold, 10000);
  EXPECT_EQ(list[0].target.priority, 2);
  EXPECT_EQ(list[0].target.ores, (std::vector{kOre}));
  EXPECT_EQ(list[0].target.product, list[0].goods.key);
  const auto events = listEvents(db(), {.source = "mining"});
  ASSERT_EQ(events.size(), 1);
  EXPECT_EQ(events[0].text, "mining target: keep 10000, priority 2, 1 ore (by pavel)");
}

UTEST_F(MiningTest, TargetRefusals) {
  EXPECT_FALSE(setMiningTarget(db(), dust, {.threshold = 0}, t0).has_value());
  EXPECT_FALSE(
      setMiningTarget(db(), dust, {.threshold = 1, .priority = 0}, t0).has_value());
  EXPECT_FALSE(setMiningTarget(db(), 999999, {.threshold = 1}, t0).has_value());
  EXPECT_FALSE(deleteMiningTarget(db(), dust, t0));
}

UTEST_F(MiningTest, SetupsSavedAndListed) {
  auto t = tx();
  EXPECT_FALSE(lockMiningSetup(t, "a"));
  saveMiningSetup(t, {.name = "a", .run = "r", .status = json::FromString("{}"),
                      .plan = json::FromString(R"([{"module": "1,2,3"}])"),
                      .seenAt = t0});
  saveMiningSetup(t, {.name = "b", .run = "r", .status = json::FromString("{}"),
                      .plan = json::FromString("[]"), .seenAt = t0});
  const auto a = lockMiningSetup(t, "a");
  ASSERT_TRUE(a);
  EXPECT_EQ(a->plan[0]["module"].As<std::string>(), "1,2,3");
  const auto others = listMiningSetups(t, "a");
  ASSERT_EQ(others.size(), 1);
  EXPECT_EQ(others[0].name, "b");
  t.Commit();
  EXPECT_TRUE(deleteMiningSetup(db(), "a", t0));
  EXPECT_EQ(listMiningSetups(db()).size(), 1);
}

UTEST_F(MiningTest, FluidStock) {
  auto t = tx();
  const auto s = fluidStock(t, {"plasma.radon", "plasma.helium"});
  EXPECT_EQ(s.size(), 1);
  EXPECT_EQ(s.at("plasma.radon"), 2'000'000);
}

const MiningItem kTip{.key = {"gregtech:gt.metaitem.02", 8129, ""},
                      .label = "Neutronium Drill Tip"};
const MiningItem kRod{.key = {"gregtech:gt.metaitem.01", 23129, ""},
                      .label = "Neutronium Rod"};

UTEST_F(MiningTest, SuppliesMadeOnceInTheirGroup) {
  const std::vector<MiningSupply> s{{kTip, 16384, 4096}, {kRod, 16384, 4096}};
  auto t = tx();
  const auto events = ensureMiningSupplies(t, s, t0);
  ASSERT_EQ(events.size(), 2);
  EXPECT_EQ(events[0].text, "target: keep 16384, order 4096 (by mining)");
  EXPECT_EQ(events[0].source, "target");
  // The next poll: nothing more.
  EXPECT_TRUE(ensureMiningSupplies(t, s, t0).empty());
  t.Commit();
  const auto targets = listTargets(db());
  ASSERT_EQ(targets.size(), 2);
  const auto groups = listGroups(db());
  ASSERT_EQ(groups.size(), 1);
  EXPECT_EQ(groups[0].name, "Mining");
  EXPECT_EQ(targets[0].settings.group, groups[0].id);
  EXPECT_EQ(targets[0].settings.note, "for mining");
  // Removed by the player: not made again.
  ASSERT_TRUE(deleteTarget(db(), targets[0].goods.goodsId, t0));
  auto t2 = tx();
  EXPECT_TRUE(ensureMiningSupplies(t2, s, t0).empty());
  t2.Commit();
  EXPECT_EQ(listTargets(db()).size(), 1);
}

UTEST_F(MiningTest, SupplyWithTheirTargetLeftAsItIs) {
  auto t = tx();
  const auto id = ensureGoods(t, kTip.key, kTip.label);
  t.Commit();
  ASSERT_TRUE(setTarget(db(), id, {.threshold = 50000, .batch = 1000}, t0));
  auto t2 = tx();
  EXPECT_TRUE(ensureMiningSupplies(t2, std::vector<MiningSupply>{{kTip, 16384, 4096}}, t0).empty());
  t2.Commit();
  EXPECT_EQ(listTargets(db()).at(0).settings.threshold, 50000);
  EXPECT_TRUE(listGroups(db()).empty());
}

UTEST_F(MiningTest, StockOfKeys) {
  auto t = tx();
  const std::vector<ItemKey> keys{{"gregtech:gt.metaitem.01", 2982, ""},
                                  {"ae2fc:fluid_drop", 0, "plasma.radon"},
                                  kTip.key};
  const auto s = miningStock(t, keys);
  EXPECT_EQ(s.size(), 2);
  EXPECT_EQ(s.at(keys[0]).amount, 5000);
  EXPECT_EQ(s.at(keys[1]).amount, 2'000'000);
}

}  // namespace
