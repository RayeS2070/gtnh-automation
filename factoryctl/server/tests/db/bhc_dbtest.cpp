#include <userver/formats/json/serialize.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/bhc.hpp"
#include "db/schema.hpp"
#include "fixture.hpp"

namespace {

using namespace factoryctl::db;
using factoryctl::agent::BhcSession;
using factoryctl::agent::Coords;
using factoryctl::domain::TimePoint;
using factoryctl::test::DbTest;
namespace json = userver::formats::json;

const TimePoint t0{std::chrono::sys_days{std::chrono::November / 1 / 2026}};

class BhcTest : public DbTest {
 protected:
  BhcTest() { (void)ensureSchema(db()); }

  auto save(const BhcSetup& s) {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    saveBhcSetup(tx, s, t0);
    tx.Commit();
  }

  auto lock(const std::string& name) {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    auto s = lockBhcSetup(tx, name);
    tx.Commit();
    return s;
  }

  auto add(const std::vector<BhcSession>& sessions, TimePoint now,
           const std::string& run = "r1") {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    auto added = addBhcSessions(tx, "bhc-main", run, sessions, now);
    tx.Commit();
    return added;
  }

  static auto setup() -> BhcSetup {
    return {.name = "bhc-main",
            .coords = Coords{1, 2, 3},
            .run = "r1",
            .status = json::FromString(
                R"({"state": {"phase": "idle"}, "supplies": {}})"),
            .seenAt = t0};
  }

  static auto session(std::int64_t n) -> BhcSession {
    return {.session = n,
            .seconds = 200,
            .holds = 120,
            .spent = 450,
            .recipes = 10,
            .ending = "collapsed"};
  }
};

UTEST_F(BhcTest, SaveAndRead) {
  EXPECT_TRUE(listBhcSetups(db()).empty());
  auto s = setup();
  s.settingsVersion = 1;
  s.settings = json::FromString(
      R"({"holdAt": 20, "floor": 2, "maxHolds": 120, "minSpacetime": 10000})");
  save(s);
  const auto back = lock("bhc-main");
  ASSERT_TRUE(back);
  EXPECT_EQ(back->coords, (Coords{1, 2, 3}));
  EXPECT_EQ(back->status["state"]["phase"].As<std::string>(), "idle");
  EXPECT_EQ(back->settingsVersion, 1);
  EXPECT_EQ((*back->settings)["holdAt"].As<int>(), 20);
  EXPECT_FALSE(lock("nobody"));
}

UTEST_F(BhcTest, SessionsOnceEachNewestFirst) {
  save(setup());
  auto first = session(1);
  first.ago = 60;
  EXPECT_EQ(add({first, session(2)}, t0).size(), 2);
  EXPECT_EQ(add({session(2), session(3)}, t0 + std::chrono::seconds(5)).size(),
            1)
      << "a session sent again is taken once";
  EXPECT_EQ(add({session(2)}, t0, "r2").size(), 1)
      << "another run numbers its own";
  const auto rows = listBhcSessions(db(), "bhc-main", 10);
  ASSERT_EQ(rows.size(), 4);
  EXPECT_EQ(rows[0].s.session, 3);
  EXPECT_EQ(rows.back().s.session, 1);
  EXPECT_EQ(rows.back().closedAt, t0 - std::chrono::seconds(60))
      << "ended `ago` before";
  EXPECT_EQ(rows[0].s.recipes, 10);
  EXPECT_EQ(listBhcSessions(db(), "bhc-main", 2).size(), 2);
}

UTEST_F(BhcTest, KeepsTheNewest) {
  save(setup());
  std::vector<BhcSession> many;
  for (std::int64_t n = 1; n <= kBhcSessionsKept + 5; ++n) {
    auto s = session(n);
    s.ago =
        static_cast<double>(kBhcSessionsKept + 5 - n);  // the higher, the newer
    many.push_back(s);
  }
  add(many, t0);
  const auto rows = listBhcSessions(db(), "bhc-main", kBhcSessionsKept + 10);
  ASSERT_EQ(static_cast<std::int64_t>(rows.size()), kBhcSessionsKept);
  EXPECT_EQ(rows.back().s.session, 6);
}

UTEST_F(BhcTest, SettingsFromTheClient) {
  auto s = setup();
  s.settingsVersion = 1;
  s.settings = json::FromString(
      R"({"holdAt": 20, "floor": 2, "maxHolds": 120, "minSpacetime": 10000})");
  save(s);
  const auto v = setBhcSettings(
      db(), "bhc-main",
      json::FromString(
          R"({"version": 9, "holdAt": 25, "floor": 3, "maxHolds": 90, "minSpacetime": 500})"),
      t0);
  ASSERT_TRUE(v.has_value()) << v.error();
  EXPECT_EQ(**v, 2);
  const auto back = lock("bhc-main");
  EXPECT_EQ((*back->settings)["holdAt"].As<int>(), 25);
  EXPECT_TRUE((*back->settings)["version"].IsMissing())
      << "the column holds it";

  const auto bad = setBhcSettings(
      db(), "bhc-main",
      json::FromString(
          R"({"holdAt": 2, "floor": 3, "maxHolds": 90, "minSpacetime": 500})"),
      t0);
  ASSERT_FALSE(bad.has_value());
  EXPECT_EQ(lock("bhc-main")->settingsVersion, 2)
      << "a refused change changes nothing";

  const auto none = setBhcSettings(
      db(), "nobody",
      json::FromString(
          R"({"holdAt": 20, "floor": 2, "maxHolds": 1, "minSpacetime": 0})"),
      t0);
  ASSERT_TRUE(none.has_value());
  EXPECT_FALSE(*none);
}

UTEST_F(BhcTest, ForgetTakesTheSessions) {
  save(setup());
  add({session(1)}, t0);
  EXPECT_TRUE(deleteBhcSetup(db(), "bhc-main", t0));
  EXPECT_FALSE(deleteBhcSetup(db(), "bhc-main", t0));
  EXPECT_TRUE(listBhcSessions(db(), "bhc-main", 10).empty());
}

}  // namespace
