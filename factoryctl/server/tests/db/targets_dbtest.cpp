#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/events.hpp"
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
  ASSERT_TRUE(setTarget(db(), stick, s, t0).has_value());
  const auto list = listTargets(db());
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list[0].goods.goodsId, stick);
  EXPECT_EQ(list[0].goods.label, "Stick");
  EXPECT_EQ(list[0].goods.amount, 5);
  EXPECT_EQ(list[0].settings, s);
}

UTEST_F(TargetsTest, EventsSayWhoDidIt) {
  ASSERT_TRUE(setTarget(db(), stick, {.threshold = 1, .batch = 1}, t0, "pavel")
                  .has_value());
  ASSERT_TRUE(deleteTarget(db(), stick, t0));
  const auto events = listEvents(db(), {.source = "target"});
  ASSERT_EQ(events.size(), 2);
  EXPECT_EQ(events[0].text, "target removed");  // no one: no proxy in front
  EXPECT_TRUE(events[1].text.ends_with(" (by pavel)")) << events[1].text;
}

UTEST_F(TargetsTest, SetAgainReplaces) {
  ASSERT_TRUE(setTarget(db(), stick, {.threshold = 1, .batch = 1}, t0).has_value());
  const TargetSettings s{.threshold = 0, .batch = 144, .enabled = false};
  ASSERT_TRUE(setTarget(db(), stick, s, t0).has_value());
  const auto list = listTargets(db());
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list[0].settings, s);
}

UTEST_F(TargetsTest, DisabledTargetsAreListedButNotPlanned) {
  ASSERT_TRUE(
      setTarget(db(), stick, {.threshold = 1, .batch = 1, .enabled = false}, t0)
          .has_value());
  ASSERT_TRUE(
      setTarget(db(), xenoxene, {.threshold = 1, .batch = 1}, t0).has_value());
  EXPECT_EQ(listTargets(db()).size(), 2);
  auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
  const auto planned = loadTargets(tx);
  ASSERT_EQ(planned.size(), 1);
  EXPECT_EQ(planned[0].item.variant, "xenoxene");
  EXPECT_FALSE(planned[0].fill);
}

UTEST_F(TargetsTest, FillReachesThePoll) {
  ASSERT_TRUE(setTarget(db(), stick, {.threshold = 1, .batch = 1, .fill = true}, t0)
                  .has_value());
  auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
  EXPECT_TRUE(loadTargets(tx).at(0).fill);
}

UTEST_F(TargetsTest, UnknownGoodsRefused) {
  const auto r = setTarget(db(), 999'999, {.threshold = 1, .batch = 1}, t0);
  ASSERT_FALSE(r.has_value());
  EXPECT_NE(r.error().find("999999"), std::string::npos) << r.error();
}

UTEST_F(TargetsTest, BadNumbersRefused) {
  EXPECT_FALSE(
      setTarget(db(), stick, {.threshold = -1, .batch = 1}, t0).has_value());
  EXPECT_FALSE(
      setTarget(db(), stick, {.threshold = 1, .batch = 0}, t0).has_value());
  EXPECT_TRUE(listTargets(db()).empty());
}

UTEST_F(TargetsTest, Delete) {
  ASSERT_TRUE(setTarget(db(), stick, {.threshold = 1, .batch = 1}, t0).has_value());
  EXPECT_TRUE(deleteTarget(db(), stick, t0));
  EXPECT_FALSE(deleteTarget(db(), stick, t0));
  EXPECT_TRUE(listTargets(db()).empty());
}

// --- groups ----------------------------------------------------------------

UTEST_F(TargetsTest, GroupsAddRenameDelete) {
  EXPECT_TRUE(listGroups(db()).empty());
  const auto plasma = addGroup(db(), "Plasma", t0);
  ASSERT_TRUE(plasma.has_value()) << plasma.error();
  const auto casings = addGroup(db(), "casings", t0);
  ASSERT_TRUE(casings.has_value());
  auto list = listGroups(db());
  ASSERT_EQ(list.size(), 2);
  EXPECT_EQ(list[0].name, "casings") << "by name, case aside";
  EXPECT_EQ(list[1].id, *plasma);
  EXPECT_EQ(list[1].targets, 0);

  const auto renamed = renameGroup(db(), *plasma, "Plasmas", t0);
  ASSERT_TRUE(renamed.has_value());
  EXPECT_TRUE(*renamed);
  EXPECT_EQ(listGroups(db())[1].name, "Plasmas");
  EXPECT_FALSE(*renameGroup(db(), 999'999, "x", t0)) << "no such group";

  EXPECT_TRUE(deleteGroup(db(), *plasma, t0));
  EXPECT_FALSE(deleteGroup(db(), *plasma, t0));
  EXPECT_EQ(listGroups(db()).size(), 1);
}

UTEST_F(TargetsTest, GroupNamesAreUnique) {
  ASSERT_TRUE(addGroup(db(), "Plasma", t0).has_value());
  const auto again = addGroup(db(), "Plasma", t0);
  ASSERT_FALSE(again.has_value());
  EXPECT_NE(again.error().find("already"), std::string::npos);
  const auto other = addGroup(db(), "Other", t0);
  const auto clash = renameGroup(db(), *other, "Plasma", t0);
  ASSERT_FALSE(clash.has_value());
  EXPECT_NE(clash.error().find("already"), std::string::npos);
  EXPECT_FALSE(addGroup(db(), "", t0).has_value());
}

UTEST_F(TargetsTest, TargetInAGroup) {
  const auto plasma = *addGroup(db(), "Plasma", t0);
  ASSERT_TRUE(setTarget(db(), xenoxene,
                        {.threshold = 1, .batch = 1, .group = plasma}, t0)
                  .has_value());
  ASSERT_TRUE(
      setTarget(db(), stick, {.threshold = 1, .batch = 1}, t0).has_value());
  const auto groupOf = [&](std::int64_t goods) {
    for (const auto& t : listTargets(db())) {
      if (t.goods.goodsId == goods) return t.settings.group;
    }
    ADD_FAILURE() << "no target for " << goods;
    return std::optional<std::int64_t>{};
  };
  EXPECT_EQ(groupOf(stick), std::nullopt);
  EXPECT_EQ(groupOf(xenoxene), plasma);
  EXPECT_EQ(listGroups(db())[0].targets, 1);

  // A deleted group leaves its targets, ungrouped: they keep working.
  ASSERT_TRUE(deleteGroup(db(), plasma, t0));
  EXPECT_EQ(listTargets(db()).size(), 2);
  EXPECT_EQ(groupOf(xenoxene), std::nullopt);
}

UTEST_F(TargetsTest, UnknownGroupRefused) {
  const auto r =
      setTarget(db(), stick, {.threshold = 1, .batch = 1, .group = 999'999}, t0);
  ASSERT_FALSE(r.has_value());
  EXPECT_NE(r.error().find("no group"), std::string::npos);
  EXPECT_TRUE(listTargets(db()).empty());
}

}  // namespace
