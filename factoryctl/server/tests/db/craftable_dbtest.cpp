#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/manual.hpp"
#include "db/orders.hpp"
#include "db/scan.hpp"
#include "db/schema.hpp"
#include "db/stock.hpp"
#include "db/targets.hpp"
#include "fixture.hpp"

namespace {

using namespace factoryctl::domain;
using namespace factoryctl::db;
using namespace std::chrono_literals;
using factoryctl::test::DbTest;

const TimePoint t0{std::chrono::sys_days{std::chrono::October / 30 / 2026}};
const ItemKey kStick{"minecraft:stick", 0, ""};
const ItemKey kDirt{"minecraft:dirt", 0, ""};
const ScannedGoods kStickGoods{kStick, "Stick", 5, true};
const ScannedGoods kDirtGoods{kDirt, "Dirt", 900, false};

class CraftableTest : public DbTest {
 protected:
  CraftableTest() {
    (void)ensureSchema(db());
    scan({kStickGoods, kDirtGoods}, t0);
    stick = goodsId("stick");
    dirt = goodsId("dirt");
  }

  auto scan(std::vector<ScannedGoods> goods, TimePoint at) -> void {
    const auto id = startScan(db(), at);
    (void)storeBatch(db(), id, goods);
    (void)finishScan(db(), id, at + 9s);
  }

  auto goodsId(std::string text) -> std::int64_t {
    return findStock(db(), {.text = std::move(text), .limit = 1}).at(0).goodsId;
  }

  auto craftable(std::string text) -> bool {
    return findStock(db(), {.text = std::move(text), .limit = 1})
        .at(0)
        .craftable;
  }

  auto uncraftable() {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    auto keys = loadUncraftable(tx);
    tx.Commit();
    std::ranges::sort(keys);
    return keys;
  }

  auto learn(std::vector<Craftability> c) -> void {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    saveCraftability(tx, c);
    tx.Commit();
  }

  std::int64_t stick = 0, dirt = 0;
};

UTEST_F(CraftableTest, ScanTellsCraftable) {
  EXPECT_TRUE(craftable("stick"));
  EXPECT_FALSE(craftable("dirt"));
  EXPECT_EQ(uncraftable(), (std::vector<ItemKey>{kDirt}));
}

UTEST_F(CraftableTest, GoneFromTheLastScanIsNotCraftable) {
  // A craftable item is always scanned, even at 0: gone means no pattern.
  scan({kDirtGoods}, t0 + 5min);
  EXPECT_FALSE(craftable("stick"));
  EXPECT_EQ(uncraftable(), (std::vector<ItemKey>{kDirt, kStick}));
}

UTEST_F(CraftableTest, NextScanUndoes) {
  learn({{kStick, false}});
  scan({kStickGoods, kDirtGoods}, t0 + 5min);
  EXPECT_TRUE(craftable("stick")) << "the pattern is back";
}

UTEST_F(CraftableTest, AgentLearnsBetweenScans) {
  learn({{kStick, false}, {kDirt, true}});
  EXPECT_FALSE(craftable("stick"));
  EXPECT_TRUE(craftable("dirt"));
  learn({{kStick, true}, {kStick, false}});
  EXPECT_FALSE(craftable("stick")) << "the last word counts";
  learn({{{"a:unknown", 0, ""}, false}});
  EXPECT_EQ(uncraftable(), (std::vector<ItemKey>{kStick})) << "unknown: left out";
}

UTEST_F(CraftableTest, TargetsAndOrdersShowIt) {
  ASSERT_TRUE(setTarget(db(), dirt, {.threshold = 1000, .batch = 64}));
  const auto targets = listTargets(db());
  ASSERT_EQ(targets.size(), 1);
  EXPECT_FALSE(targets[0].goods.craftable);

  ASSERT_TRUE(orderOnce(db(), stick, 64, "", t0));
  const auto orders = listOrders(db(), 10);
  ASSERT_EQ(orders.size(), 1);
  EXPECT_TRUE(orders[0].goods.craftable);
}

UTEST_F(CraftableTest, OneOffOrderOfUncraftableIsRefused) {
  const auto id = orderOnce(db(), dirt, 64, "", t0);
  ASSERT_FALSE(id.has_value());
  EXPECT_NE(id.error().find("craft"), std::string::npos) << id.error();
  EXPECT_TRUE(listOrders(db(), 10).empty());
}

}  // namespace
