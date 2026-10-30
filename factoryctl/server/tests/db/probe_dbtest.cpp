#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/manual.hpp"
#include "db/orders.hpp"
#include "db/scan.hpp"
#include "db/schema.hpp"
#include "db/stock.hpp"
#include "fixture.hpp"

namespace {

using namespace factoryctl::domain;
using namespace factoryctl::db;
using namespace std::chrono_literals;
using factoryctl::test::DbTest;

const TimePoint t0{std::chrono::sys_days{std::chrono::October / 30 / 2026}};
const ItemKey kRod{"bartworks:gt.bwMetaGeneratedstickLong", 11358, ""};
const ItemKey kPersulfate{"ae2fc:fluid_drop", 0, "sodiumpersulfate"};

class ProbeTest : public DbTest {
 protected:
  ProbeTest() {
    (void)ensureSchema(db());
    scan({{kRod, "Long Lanthanum Hexaboride Rod", 55'452},
          {kPersulfate, "drop of Sodium Persulfate", 1'366'000}},
         t0);
    rod = findStock(db(), {.text = "lanthanum", .limit = 1}).at(0).goodsId;
  }

  auto scan(std::vector<ScannedGoods> goods, TimePoint at) -> void {
    const auto id = startScan(db(), at);
    (void)storeBatch(db(), id, goods);
    (void)finishScan(db(), id, at + 9s);
  }

  auto probe(std::vector<ProbedAmount> probes, TimePoint at) -> void {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    applyProbes(tx, probes, at);
    tx.Commit();
  }

  auto settling(TimePoint stockAsOf) {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    auto keys = loadSettling(tx, stockAsOf);
    tx.Commit();
    return keys;
  }

  auto closeRodOrder(TimePoint at) -> void {
    const auto id = orderOnce(db(), rod, 500, "", at);
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    auto orders = loadOpenOrders(tx);
    for (auto& o : orders) {
      if (o.id == *id) {
        o.status = OrderStatus::kDone;
        o.changedAt = at;
      }
    }
    saveOrders(tx, orders);
    tx.Commit();
  }

  std::int64_t rod = 0;
};

UTEST_F(ProbeTest, ProbeUpdatesTheStock) {
  probe({{kRod, 61'097}, {kPersulfate, 1'220'600}}, t0 + 1min);
  const auto stock = currentStock(db());
  ASSERT_TRUE(stock.has_value());
  EXPECT_EQ(stock->at(kRod), 61'097);
  EXPECT_EQ(stock->at(kPersulfate), 1'220'600);
}

UTEST_F(ProbeTest, ProbedGoodsCountAfterAScanMissedThem) {
  scan({{kPersulfate, "drop of Sodium Persulfate", 1}}, t0 + 5min);
  EXPECT_FALSE(currentStock(db())->contains(kRod)) << "the scan says 0";
  probe({{kRod, 500}}, t0 + 6min);
  EXPECT_EQ(currentStock(db())->at(kRod), 500);
}

UTEST_F(ProbeTest, UnknownGoodsAreLeftOut) {
  probe({{{"new:item", 0, ""}, 5}}, t0 + 1min);
  EXPECT_FALSE(currentStock(db())->contains(ItemKey{"new:item", 0, ""}));
}

UTEST_F(ProbeTest, AProbeAfterTheOrderEndsTheSettling) {
  closeRodOrder(t0 + 2min);
  EXPECT_EQ(settling(t0), (std::vector<ItemKey>{kRod})) << "the scan is older";
  probe({{kRod, 61'097}}, t0 + 2min + 10s);
  EXPECT_TRUE(settling(t0).empty()) << "measured after the order closed";
}

UTEST_F(ProbeTest, AProbeBeforeTheOrderDoesNot) {
  probe({{kRod, 55'452}}, t0 + 1min);
  closeRodOrder(t0 + 2min);
  EXPECT_EQ(settling(t0), (std::vector<ItemKey>{kRod}));
}

}  // namespace
