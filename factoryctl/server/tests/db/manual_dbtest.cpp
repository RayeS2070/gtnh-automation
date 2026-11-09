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

class ManualTest : public DbTest {
 protected:
  ManualTest() {
    (void)ensureSchema(db());
    const auto id = startScan(db(), t0);
    const std::vector<ScannedGoods> goods{
        {{"gregtech:gt.blockcasings10", 11, ""},
         "Extreme Density Space-Bending Casing",
         1571},
        {{"ae2fc:fluid_drop", 0, "sodiumpersulfate"},
         "drop of Sodium Persulfate",
         1'366'000}};
    (void)storeBatch(db(), id, goods);
    (void)finishScan(db(), id, t0);
    casing = findStock(db(), {.text = "casing", .limit = 1}).at(0).goodsId;
    persulfate = findStock(db(), {.text = "persulfate", .limit = 1}).at(0).goodsId;
  }

  // Moves an order as a poll would.
  auto setStatus(std::int64_t id, OrderStatus status, std::string cpu = "") {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    auto orders = loadOpenOrders(tx);
    for (auto& o : orders) {
      if (o.id == id) {
        o.status = status;
        o.cpu = std::move(cpu);
        break;  // ids are unique
      }
    }
    saveOrders(tx, orders);
    tx.Commit();
  }

  std::int64_t casing = 0, persulfate = 0;
};

UTEST_F(ManualTest, OneOffOrderIsQueued) {
  const auto id = orderOnce(db(), casing, 128, "Pavel", t0);
  ASSERT_TRUE(id.has_value()) << id.error();
  const auto list = listOrders(db(), 10);
  ASSERT_EQ(list.size(), 1);
  const auto& o = list[0];
  EXPECT_EQ(o.id, *id);
  EXPECT_EQ(o.goods.label, "Extreme Density Space-Bending Casing");
  EXPECT_EQ(o.goods.amount, 1571);
  EXPECT_EQ(o.amount, 128);
  EXPECT_EQ(o.status, "queued");
  EXPECT_EQ(o.source, "manual");
  EXPECT_EQ(o.requestedBy, "Pavel");
  EXPECT_EQ(o.createdAt, t0);
}

UTEST_F(ManualTest, ThePollSendsIt) {
  ASSERT_TRUE(orderOnce(db(), persulfate, 10'000, "", t0).has_value());
  auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
  const auto open = loadOpenOrders(tx);
  ASSERT_EQ(open.size(), 1);
  EXPECT_EQ(open[0].item.variant, "sodiumpersulfate");
  EXPECT_EQ(open[0].amount, 10'000);
}

UTEST_F(ManualTest, OneOpenOrderPerGoods) {
  ASSERT_TRUE(orderOnce(db(), casing, 128, "", t0).has_value());
  const auto second = orderOnce(db(), casing, 64, "", t0);
  ASSERT_FALSE(second.has_value());
  EXPECT_NE(second.error().find("already"), std::string::npos)
      << second.error();
  EXPECT_TRUE(orderOnce(db(), persulfate, 1, "", t0).has_value())
      << "other goods are free";
}

UTEST_F(ManualTest, ClosedOrderFreesTheGoods) {
  const auto id = orderOnce(db(), casing, 128, "", t0);
  setStatus(*id, OrderStatus::kDone);
  EXPECT_TRUE(orderOnce(db(), casing, 64, "", t0).has_value());
}

UTEST_F(ManualTest, BadInputRefused) {
  EXPECT_FALSE(orderOnce(db(), casing, 0, "", t0).has_value());
  EXPECT_FALSE(orderOnce(db(), casing, -5, "", t0).has_value());
  const auto unknown = orderOnce(db(), 999'999, 1, "", t0);
  ASSERT_FALSE(unknown.has_value());
  EXPECT_NE(unknown.error().find("999999"), std::string::npos)
      << unknown.error();
  EXPECT_TRUE(listOrders(db(), 10).empty());
}

UTEST_F(ManualTest, NewestFirstAndLimit) {
  const auto a = orderOnce(db(), casing, 1, "", t0);
  const auto b = orderOnce(db(), persulfate, 1, "", t0 + 1min);
  const auto list = listOrders(db(), 10);
  ASSERT_EQ(list.size(), 2);
  EXPECT_EQ(list[0].id, *b);
  EXPECT_EQ(list[1].id, *a);
  EXPECT_EQ(listOrders(db(), 1).size(), 1);
}

UTEST_F(ManualTest, CancelQueuedAtOnce) {
  const auto id = orderOnce(db(), casing, 128, "", t0);
  EXPECT_EQ(cancelOrder(db(), *id, t0 + 1s), CancelAction::kCanceledNow);
  EXPECT_EQ(listOrders(db(), 1).at(0).status, "canceled");
  EXPECT_TRUE(orderOnce(db(), casing, 1, "", t0).has_value()) << "goods freed";
}

UTEST_F(ManualTest, CancelCraftingAsksTheAgent) {
  const auto id = orderOnce(db(), casing, 128, "", t0);
  setStatus(*id, OrderStatus::kCrafting, "CPU_022");
  EXPECT_EQ(cancelOrder(db(), *id, t0 + 1s), CancelAction::kAskAgent);
  const auto o = listOrders(db(), 1).at(0);
  EXPECT_EQ(o.status, "crafting") << "canceled only when the agent says so";
  EXPECT_TRUE(o.cancelRequested);
  EXPECT_EQ(cancelOrder(db(), *id, t0 + 2s), CancelAction::kNone)
      << "asked before";
}

UTEST_F(ManualTest, CancelClosedOrUnknown) {
  const auto id = orderOnce(db(), casing, 128, "", t0);
  setStatus(*id, OrderStatus::kDone);
  EXPECT_EQ(cancelOrder(db(), *id, t0), CancelAction::kNone);
  EXPECT_FALSE(cancelOrder(db(), 999'999, t0).has_value());
}

}  // namespace
