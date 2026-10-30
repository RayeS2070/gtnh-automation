#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/orders.hpp"
#include "db/scan.hpp"
#include "db/schema.hpp"
#include "db/targets.hpp"
#include "fixture.hpp"

namespace {

using namespace factoryctl::domain;
using namespace factoryctl::db;
using factoryctl::test::DbTest;

const TimePoint t0{std::chrono::sys_days{std::chrono::October / 30 / 2026}};

class TargetsTest : public DbTest {
 protected:
  TargetsTest() {
    (void)ensureSchema(db());
    const auto id = startScan(db(), t0);
    const std::vector<ScannedGoods> goods{
        {{"minecraft:stick", 0, ""}, "Stick", 5},
        {{"ae2fc:fluid_drop", 0, "xenoxene"}, "drop of Xenoxene", 1000}};
    (void)storeBatch(db(), id, goods);
    (void)finishScan(db(), id, t0);
    stick = findStock(db(), {.text = "stick", .limit = 1}).at(0).goodsId;
    xenoxene = findStock(db(), {.text = "xenoxene", .limit = 1}).at(0).goodsId;
  }

  std::int64_t stick = 0, xenoxene = 0;
};

UTEST_F(TargetsTest, NoneAtFirst) { EXPECT_TRUE(listTargets(db()).empty()); }

UTEST_F(TargetsTest, SetAndList) {
  const TargetSettings s{.threshold = 100,
                         .batch = 64,
                         .priority = Priority::kLow,
                         .enabled = true,
                         .note = "for the assembler",
                         .fill = true};
  ASSERT_TRUE(setTarget(db(), stick, s).has_value());
  const auto list = listTargets(db());
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list[0].goods.goodsId, stick);
  EXPECT_EQ(list[0].goods.label, "Stick");
  EXPECT_EQ(list[0].goods.amount, 5);
  EXPECT_EQ(list[0].settings, s);
}

UTEST_F(TargetsTest, SetAgainReplaces) {
  ASSERT_TRUE(setTarget(db(), stick, {.threshold = 1, .batch = 1}).has_value());
  const TargetSettings s{.threshold = 0, .batch = 144, .enabled = false};
  ASSERT_TRUE(setTarget(db(), stick, s).has_value());
  const auto list = listTargets(db());
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list[0].settings, s);
}

UTEST_F(TargetsTest, DisabledTargetsAreListedButNotPlanned) {
  ASSERT_TRUE(
      setTarget(db(), stick, {.threshold = 1, .batch = 1, .enabled = false})
          .has_value());
  ASSERT_TRUE(
      setTarget(db(), xenoxene, {.threshold = 1, .batch = 1}).has_value());
  EXPECT_EQ(listTargets(db()).size(), 2);
  auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
  const auto planned = loadTargets(tx);
  ASSERT_EQ(planned.size(), 1);
  EXPECT_EQ(planned[0].item.variant, "xenoxene");
  EXPECT_FALSE(planned[0].fill);
}

UTEST_F(TargetsTest, FillReachesThePoll) {
  ASSERT_TRUE(setTarget(db(), stick, {.threshold = 1, .batch = 1, .fill = true})
                  .has_value());
  auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
  EXPECT_TRUE(loadTargets(tx).at(0).fill);
}

UTEST_F(TargetsTest, UnknownGoodsRefused) {
  const auto r = setTarget(db(), 999'999, {.threshold = 1, .batch = 1});
  ASSERT_FALSE(r.has_value());
  EXPECT_NE(r.error().find("999999"), std::string::npos) << r.error();
}

UTEST_F(TargetsTest, BadNumbersRefused) {
  EXPECT_FALSE(
      setTarget(db(), stick, {.threshold = -1, .batch = 1}).has_value());
  EXPECT_FALSE(
      setTarget(db(), stick, {.threshold = 1, .batch = 0}).has_value());
  EXPECT_TRUE(listTargets(db()).empty());
}

UTEST_F(TargetsTest, Delete) {
  ASSERT_TRUE(setTarget(db(), stick, {.threshold = 1, .batch = 1}).has_value());
  EXPECT_TRUE(deleteTarget(db(), stick));
  EXPECT_FALSE(deleteTarget(db(), stick));
  EXPECT_TRUE(listTargets(db()).empty());
}

}  // namespace
