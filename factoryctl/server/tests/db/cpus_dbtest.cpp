#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/cpus.hpp"
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

const TimePoint t0{std::chrono::sys_days{std::chrono::October / 31 / 2026}};
const ItemKey kCasing{"gregtech:gt.blockcasings10", 11, ""};
const ItemKey kXenoxene{"ae2fc:fluid_drop", 0, "xenoxene"};

class CpusTest : public DbTest {
 protected:
  CpusTest() {
    (void)ensureSchema(db());
    const auto id = startScan(db(), t0);
    const std::vector<ScannedGoods> goods{
        {kCasing, "Extreme Density Space-Bending Casing", 1571},
        {kXenoxene, "drop of Xenoxene", 0}};
    (void)storeBatch(db(), id, goods);
    (void)finishScan(db(), id, t0);
    casing = findStock(db(), {.text = "casing", .limit = 1}).at(0).goodsId;
  }

  auto poll(std::vector<Cpu> cpus, TimePoint at) -> void {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    saveCpus(tx, cpus, at);
    tx.Commit();
  }

  std::int64_t casing = 0;
};

UTEST_F(CpusTest, NothingBeforeAPoll) { EXPECT_TRUE(listCpus(db()).empty()); }

UTEST_F(CpusTest, EveryCpuByName) {
  poll({{.busy = true, .crafting = kXenoxene, .name = "CPU_002",
         .remaining = 1'000'000},
        {.name = "CPU_001"}},
       t0 + 1min);
  const auto cpus = listCpus(db());
  ASSERT_EQ(cpus.size(), 2);
  EXPECT_EQ(cpus[0].name, "CPU_001");
  EXPECT_FALSE(cpus[0].busy);
  EXPECT_FALSE(cpus[0].goods.has_value()) << "idle";
  EXPECT_EQ(cpus[1].name, "CPU_002");
  EXPECT_TRUE(cpus[1].busy);
  ASSERT_TRUE(cpus[1].goods.has_value());
  EXPECT_EQ(cpus[1].goods->key, kXenoxene);
  EXPECT_EQ(cpus[1].goods->label, "drop of Xenoxene");
  EXPECT_EQ(cpus[1].remaining, 1'000'000);
  EXPECT_EQ(cpus[1].seenAt, t0 + 1min);
  EXPECT_EQ(cpus[1].order, std::nullopt) << "put on a CPU in game";
}

UTEST_F(CpusTest, OurOpenOrderOfThoseGoods) {
  const auto id = orderOnce(db(), casing, 128, "Pavel", t0);
  ASSERT_TRUE(id.has_value()) << id.error();
  poll({{.busy = true, .crafting = kCasing, .name = "CPU_022",
         .remaining = 100}},
       t0 + 1min);
  EXPECT_EQ(listCpus(db()).at(0).order, *id);

  // Closed: the craft on the CPU is someone else's now.
  auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
  auto orders = loadOpenOrders(tx);
  orders.at(0).status = OrderStatus::kDone;
  saveOrders(tx, orders);
  tx.Commit();
  EXPECT_EQ(listCpus(db()).at(0).order, std::nullopt);
}

UTEST_F(CpusTest, GoodsNoScanHasSeen) {
  poll({{.busy = true, .crafting = ItemKey{"a:new", 0, ""}, .name = "CPU_005",
         .remaining = 3}},
       t0 + 1min);
  const auto cpus = listCpus(db());
  ASSERT_EQ(cpus.size(), 1);
  EXPECT_TRUE(cpus[0].busy);
  EXPECT_FALSE(cpus[0].goods.has_value());
  EXPECT_EQ(cpus[0].remaining, 3);
}

}  // namespace
