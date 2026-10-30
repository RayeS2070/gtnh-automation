#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/manual.hpp"
#include "db/orders.hpp"
#include "db/scan.hpp"
#include "db/schema.hpp"
#include "fixture.hpp"

namespace {

using namespace factoryctl::domain;
using namespace factoryctl::db;
using namespace std::chrono_literals;
using factoryctl::test::DbTest;

const TimePoint t0{std::chrono::sys_days{std::chrono::October / 30 / 2026}};
const ItemKey kRod{"gregtech:gt.metaitem.01", 23324, ""};
const ItemKey kStick{"minecraft:stick", 0, ""};

class SettlingTest : public DbTest {
 protected:
  SettlingTest() {
    (void)ensureSchema(db());
    const auto id = startScan(db(), t0);
    const std::vector<ScannedGoods> goods{
        {kRod, "Long Lanthanum Hexaboride Rod", 5}, {kStick, "Stick", 5}};
    (void)storeBatch(db(), id, goods);
    (void)finishScan(db(), id, t0);
    rod = findStock(db(), {.text = "lanthanum", .limit = 1}).at(0).goodsId;
    stick = findStock(db(), {.text = "stick", .limit = 1}).at(0).goodsId;
  }

  // An order that closed with `status` at `at`.
  auto closed(std::int64_t goods, OrderStatus status, TimePoint at) {
    const auto id = orderOnce(db(), goods, 500, "", at);
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    auto orders = loadOpenOrders(tx);
    for (auto& o : orders) {
      if (o.id == *id) {
        o.status = status;
        o.changedAt = at;
      }
    }
    saveOrders(tx, orders);
    tx.Commit();
  }

  auto settlingSince(TimePoint stockAsOf) {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    auto keys = loadSettling(tx, stockAsOf);
    tx.Commit();
    return keys;
  }

  std::int64_t rod = 0, stick = 0;
};

UTEST_F(SettlingTest, ClosedAfterTheScanStartedSettles) {
  closed(rod, OrderStatus::kDone, t0 + 4min);
  EXPECT_EQ(settlingSince(t0), (std::vector<ItemKey>{kRod}));
}

UTEST_F(SettlingTest, ClosedBeforeTheScanStartedDoesNot) {
  // The scan started after the order closed: its stock shows the result.
  closed(rod, OrderStatus::kDone, t0 + 4min);
  EXPECT_TRUE(settlingSince(t0 + 5min).empty());
}

UTEST_F(SettlingTest, AnyClosedOrderCounts) {
  // A canceled or lost craft may have made part of it as well.
  closed(rod, OrderStatus::kCanceled, t0 + 1min);
  closed(stick, OrderStatus::kLost, t0 + 1min);
  auto keys = settlingSince(t0);
  std::ranges::sort(keys);
  EXPECT_EQ(keys, (std::vector<ItemKey>{kRod, kStick}));
}

UTEST_F(SettlingTest, OpenOrdersAreNotSettling) {
  ASSERT_TRUE(orderOnce(db(), rod, 500, "", t0 + 1min).has_value());
  EXPECT_TRUE(settlingSince(t0).empty()) << "open ones block by being open";
}

UTEST_F(SettlingTest, EachGoodsOnce) {
  closed(rod, OrderStatus::kDone, t0 + 1min);
  closed(rod, OrderStatus::kDone, t0 + 2min);
  EXPECT_EQ(settlingSince(t0).size(), 1);
}

}  // namespace
