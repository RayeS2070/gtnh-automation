#include <userver/formats/json/serialize.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/qft.hpp"
#include "db/schema.hpp"
#include "fixture.hpp"

namespace {

using namespace factoryctl::db;
using factoryctl::agent::Coords;
using factoryctl::domain::TimePoint;
using factoryctl::test::DbTest;
namespace json = userver::formats::json;

const TimePoint t0{std::chrono::sys_days{std::chrono::November / 1 / 2026}};

class QftTest : public DbTest {
 protected:
  QftTest() { (void)ensureSchema(db()); }

  auto save(const QftSetup& s) {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    saveQftSetup(tx, s, t0);
    tx.Commit();
  }

  auto lock(const std::string& name) {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    auto s = lockQftSetup(tx, name);
    tx.Commit();
    return s;
  }

  auto setup() -> QftSetup {
    return {.name = "qft-main",
            .coords = Coords{1, 2, 3},
            .run = "r1",
            .catalog = json::FromString(
                R"([{"id": "glue", "outputs": [{"circuit": 1, "target": 100}]}])"),
            .status = json::FromString(R"({"state": {"phase": "idle"}})"),
            .seenAt = t0};
  }
};

UTEST_F(QftTest, NoneAtFirst) {
  EXPECT_TRUE(listQftSetups(db()).empty());
  EXPECT_FALSE(lock("qft-main"));
}

UTEST_F(QftTest, SaveAndRead) {
  auto s = setup();
  s.settingsVersion = 1;
  s.settings = json::FromString(R"({"mode": "auto", "recipes": []})");
  save(s);
  const auto back = lock("qft-main");
  ASSERT_TRUE(back);
  EXPECT_EQ(back->coords, (Coords{1, 2, 3}));
  EXPECT_EQ(back->run, "r1");
  EXPECT_EQ(back->catalog[0]["id"].As<std::string>(), "glue");
  EXPECT_EQ(back->status["state"]["phase"].As<std::string>(), "idle");
  EXPECT_EQ(back->settingsVersion, 1);
  ASSERT_TRUE(back->settings);
  EXPECT_EQ((*back->settings)["mode"].As<std::string>(), "auto");

  s.coords.reset();
  s.settings.reset();
  s.settingsVersion = 0;
  s.name = "qft-two";
  save(s);
  const auto two = lock("qft-two");
  ASSERT_TRUE(two);
  EXPECT_FALSE(two->coords);
  EXPECT_FALSE(two->settings);
  EXPECT_EQ(listQftSetups(db()).size(), 2);
}

UTEST_F(QftTest, SettingsFromTheClient) {
  auto s = setup();
  s.settingsVersion = 1;
  s.settings = json::FromString(R"({"mode": "auto", "recipes": []})");
  save(s);
  const auto v = setQftSettings(db(), "qft-main", json::FromString(
      R"({"mode": "deficit", "version": 99, "recipes": [{"id": "glue", "outputs": [
          {"circuit": 1, "target": 500}]}]})"), t0);
  ASSERT_TRUE(v.has_value()) << v.error();
  EXPECT_EQ(**v, 2);
  const auto back = lock("qft-main");
  EXPECT_EQ((*back->settings)["mode"].As<std::string>(), "deficit");
  EXPECT_TRUE((*back->settings)["version"].IsMissing()) << "the column holds it";

  const auto bad = setQftSettings(db(), "qft-main", json::FromString(
      R"({"mode": "auto", "recipes": [{"id": "gone"}]})"), t0);
  ASSERT_FALSE(bad.has_value());
  EXPECT_NE(bad.error().find("no recipe"), std::string::npos);
  EXPECT_EQ(lock("qft-main")->settingsVersion, 2) << "a refused change changes nothing";

  const auto none = setQftSettings(db(), "nobody", json::FromString(R"({"mode": "auto"})"), t0);
  ASSERT_TRUE(none.has_value());
  EXPECT_FALSE(*none);
}

UTEST_F(QftTest, Forget) {
  save(setup());
  EXPECT_TRUE(deleteQftSetup(db(), "qft-main", t0));
  EXPECT_FALSE(deleteQftSetup(db(), "qft-main", t0));
  EXPECT_TRUE(listQftSetups(db()).empty());
}

}  // namespace
