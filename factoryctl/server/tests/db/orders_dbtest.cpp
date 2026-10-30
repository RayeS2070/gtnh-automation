#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/orders.hpp"
#include "db/scan.hpp"
#include "db/schema.hpp"
#include "fixture.hpp"

namespace {

using namespace factoryctl::domain;
using namespace factoryctl::db;
using namespace std::chrono_literals;
using factoryctl::test::DbTest;

const ItemKey kStick{"minecraft:stick", 0, ""};
const ItemKey kXenoxene{"ae2fc:fluid_drop", 0, "xenoxene"};
const TimePoint t0{std::chrono::sys_days{std::chrono::October / 30 / 2026}};

class OrdersTest : public DbTest {
 protected:
  OrdersTest() {
    (void)ensureSchema(db());
    // Goods come from a scan.
    const auto id = startScan(db(), t0);
    const std::vector<ScannedGoods> goods{
        {kStick, "Stick", 5}, {kXenoxene, "drop of Xenoxene", 1000}};
    (void)storeBatch(db(), id, goods);
    (void)finishScan(db(), id, t0);
  }

  auto tx() { return db().Begin(pg::ClusterHostType::kMaster, {}); }

  auto exec(const char* sql) {
    db().Execute(pg::ClusterHostType::kMaster, sql);
  }

  auto make(std::initializer_list<NewOrder> orders) -> std::int64_t {
    auto t = tx();
    const std::vector<NewOrder> v(orders);
    const auto n = insertOrders(t, v, t0);
    t.Commit();
    return n;
  }

  auto open() -> std::vector<OrderState> {
    auto t = tx();
    auto orders = loadOpenOrders(t);
    t.Commit();
    return orders;
  }
};

UTEST_F(OrdersTest, NewOrdersAreQueued) {
  EXPECT_EQ(make({{kStick, 1000}, {kXenoxene, 144}}), 2);
  const auto orders = open();
  ASSERT_EQ(orders.size(), 2);
  EXPECT_EQ(orders[0].item, kStick);
  EXPECT_EQ(orders[0].label, "Stick");
  EXPECT_EQ(orders[0].amount, 1000);
  EXPECT_EQ(orders[0].status, OrderStatus::kQueued);
  EXPECT_EQ(orders[0].changedAt, t0);
  EXPECT_EQ(orders[1].item, kXenoxene);
  EXPECT_EQ(orders[1].label, "drop of Xenoxene");
}

UTEST_F(OrdersTest, SecondOpenOrderIsSkipped) {
  EXPECT_EQ(make({{kStick, 1000}}), 1);
  EXPECT_EQ(make({{kStick, 500}, {kXenoxene, 144}}), 1);
  EXPECT_EQ(open().size(), 2);
}

UTEST_F(OrdersTest, SavedStateComesBack) {
  make({{kStick, 1000}});
  auto orders = open();
  auto& o = orders[0];
  o.status = OrderStatus::kCrafting;
  o.changedAt = t0 + 5s;
  o.cpu = "CPU_022";
  o.remaining = 996;
  o.progressAt = t0 + 6s;
  o.cancelRequested = true;
  o.alarmRaised = true;
  {
    auto t = tx();
    saveOrders(t, orders);
    t.Commit();
  }
  const auto back = open();
  ASSERT_EQ(back.size(), 1);
  EXPECT_EQ(back[0].id, o.id);
  EXPECT_EQ(back[0].status, OrderStatus::kCrafting);
  EXPECT_EQ(back[0].changedAt, t0 + 5s);
  EXPECT_EQ(back[0].cpu, "CPU_022");
  EXPECT_EQ(back[0].remaining, 996);
  EXPECT_EQ(back[0].progressAt, t0 + 6s);
  EXPECT_TRUE(back[0].cancelRequested);
  EXPECT_TRUE(back[0].alarmRaised);
}

UTEST_F(OrdersTest, EveryStatusRoundTrips) {
  // The C++ enum and the database enum must agree name by name.
  for (const auto s :
       {OrderStatus::kQueued, OrderStatus::kSent, OrderStatus::kComputing,
        OrderStatus::kAccepted, OrderStatus::kCrafting}) {
    exec("DELETE FROM factoryctl.orders");
    make({{kStick, 1}});
    auto orders = open();
    orders[0].status = s;
    auto t = tx();
    saveOrders(t, orders);
    t.Commit();
    EXPECT_EQ(open().at(0).status, s) << static_cast<int>(s);
  }
}

UTEST_F(OrdersTest, ClosedOrdersAreNotLoaded) {
  make({{kStick, 1000}});
  auto orders = open();
  orders[0].status = OrderStatus::kDone;
  auto t = tx();
  saveOrders(t, orders);
  t.Commit();
  EXPECT_TRUE(open().empty());
  EXPECT_EQ(make({{kStick, 1000}}), 1) << "done frees the goods";
}

UTEST_F(OrdersTest, Targets) {
  exec(
      "INSERT INTO factoryctl.targets (goods_id, threshold, batch, priority) "
      "SELECT id, 100, 1000, 'low' FROM factoryctl.goods "
      "WHERE name = 'minecraft:stick'");
  exec(
      "INSERT INTO factoryctl.targets (goods_id, threshold, batch, enabled) "
      "SELECT id, 0, 144, false FROM factoryctl.goods "
      "WHERE variant = 'xenoxene'");
  auto t = tx();
  const auto targets = loadTargets(t);
  ASSERT_EQ(targets.size(), 1) << "disabled ones are left out";
  EXPECT_EQ(targets[0].item, kStick);
  EXPECT_EQ(targets[0].threshold, 100);
  EXPECT_EQ(targets[0].batch, 1000);
  EXPECT_EQ(targets[0].priority, Priority::kLow);
}

UTEST_F(OrdersTest, CpusAreReplaced) {
  const auto count = [&](const char* sql) {
    return db()
        .Execute(pg::ClusterHostType::kMaster, sql)
        .AsSingleRow<std::int64_t>();
  };
  {
    auto t = tx();
    const std::vector<Cpu> cpus{{.busy = true,
                                 .crafting = kXenoxene,
                                 .name = "CPU_022",
                                 .remaining = 1'000'000},
                                {.busy = false, .name = "CPU_018"}};
    saveCpus(t, cpus, t0);
    t.Commit();
  }
  EXPECT_EQ(count("SELECT count(*) FROM factoryctl.cpus"), 2);
  EXPECT_EQ(
      count("SELECT count(*) FROM factoryctl.cpus WHERE goods_id IS NOT NULL"),
      1);
  {
    auto t = tx();
    const std::vector<Cpu> cpus{{.busy = false, .name = "CPU_001"}};
    saveCpus(t, cpus, t0 + 10s);
    t.Commit();
  }
  EXPECT_EQ(count("SELECT count(*) FROM factoryctl.cpus"), 1);
}

UTEST_F(OrdersTest, CpuCraftingUnknownGoods) {
  // A craft of something no scan has seen yet: kept, without goods.
  auto t = tx();
  const std::vector<Cpu> cpus{{.busy = true,
                               .crafting = ItemKey{"new:item", 0, ""},
                               .name = "CPU_005",
                               .remaining = 3}};
  saveCpus(t, cpus, t0);
  t.Commit();
  EXPECT_EQ(db().Execute(pg::ClusterHostType::kMaster,
                         "SELECT count(*) FROM factoryctl.cpus")
                .AsSingleRow<std::int64_t>(),
            1);
}

UTEST_F(OrdersTest, ScanState) {
  {
    auto t = tx();
    const auto s = scanState(t);
    EXPECT_EQ(s.lastFinished, t0) << "the fixture's scan";
    EXPECT_EQ(s.running, std::nullopt);
  }
  startScan(db(), t0 + 5min);
  auto t = tx();
  const auto s = scanState(t);
  EXPECT_EQ(s.lastFinished, t0);
  EXPECT_EQ(s.running, t0 + 5min);
}

}  // namespace
