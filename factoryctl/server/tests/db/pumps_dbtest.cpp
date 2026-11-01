#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/events.hpp"
#include "db/pumps.hpp"
#include "db/schema.hpp"
#include "fixture.hpp"

namespace {

using namespace factoryctl::domain;
using namespace factoryctl::db;
using factoryctl::test::DbTest;
using namespace std::chrono_literals;

const TimePoint t0{std::chrono::sys_days{std::chrono::November / 1 / 2026}};

class PumpsTest : public DbTest {
 protected:
  PumpsTest() { (void)ensureSchema(db()); }

  auto store(const PumpReport& r, TimePoint at = t0) -> std::vector<Event> {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    auto events = storePumpsReport(tx, r, at);
    tx.Commit();
    return events;
  }

  auto settings() -> std::optional<PumpSettings> {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    auto s = loadPumpSettings(tx);
    tx.Commit();
    return s;
  }

  auto withRecipes() -> PumpReport {
    return {.run = "r1",
            .recipes = std::vector<PumpRecipe>{{4, 3, "oil", "Oil", 1'400'000},
                                               {5, 4, "helium", "Helium", 1'400'000}}};
  }

  auto fluid(const PumpsView& v, std::string_view name) -> const PumpFluidRow* {
    for (const auto& f : v.fluids) {
      if (f.recipe.fluid == name) return &f;
    }
    return nullptr;
  }
};

UTEST_F(PumpsTest, NoSettingsAtFirst) {
  EXPECT_EQ(settings(), std::nullopt);
  EXPECT_EQ(pumpsView(db(), t0).settings, std::nullopt);
}

UTEST_F(PumpsTest, StoredSettingsComeBack) {
  const PumpSettings s{.version = 1,
                       .batch = 20,
                       .threshold = 0.9,
                       .targets = {{"helium", 1000, 3, true}, {"oil", 5, 1, false}}};
  {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    storePumpSettings(tx, s, t0);
    tx.Commit();
  }
  EXPECT_EQ(settings(), s);
}

UTEST_F(PumpsTest, AReport) {
  auto r = withRecipes();
  r.modules = {{"aaaa1111", 1, "running", {{"oil", 4, 0}}, 200, 600},
               {"bbbb2222", 1, "idle", {}, std::nullopt, std::nullopt}};
  r.cycles = {{"r1", 1, "aaaa1111", 30, "done", "", 30.5, {{"oil", 4, 168'000'000}}},
              {"r1", 2, "bbbb2222", 30, "failed", "did not start", std::nullopt, {}}};
  r.stock = {{"oil", 5'000}, {"helium", 0}};
  const auto events = store(r);
  ASSERT_EQ(events.size(), 2) << "two modules came";
  EXPECT_NE(events[0].text.find("aaaa1111"), std::string::npos);

  {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    storePumpSettings(tx, {.version = 1, .targets = {{"oil", 1'000'000, 1, true}}}, t0);
    tx.Commit();
  }
  const auto v = pumpsView(db(), t0 - 1h);
  ASSERT_EQ(v.modules.size(), 2);
  EXPECT_EQ(v.modules[0].module.progress, 200);
  EXPECT_EQ(v.modules[0].module.slots.at(0).fluid, "oil");
  EXPECT_EQ(v.failed, 1);
  const auto* oil = fluid(v, "oil");
  ASSERT_NE(oil, nullptr);
  EXPECT_EQ(oil->amount, 5'000);
  EXPECT_EQ(oil->produced, 168'000'000);
  EXPECT_EQ(oil->inFlight, 1'400'000LL * 4 * 30) << "the running module";
  ASSERT_TRUE(oil->target);
  EXPECT_EQ(oil->target->target, 1'000'000);
  const auto* helium = fluid(v, "helium");
  ASSERT_NE(helium, nullptr);
  EXPECT_FALSE(helium->target);
  EXPECT_EQ(helium->inFlight, 0);
  EXPECT_EQ(fluid(v, "lava"), nullptr) << "only the recipes the agent sent";

  // The same poll again (its reply was lost): cycles count once.
  EXPECT_TRUE(store(r).empty()) << "nothing new: no events";
  EXPECT_EQ(fluid(pumpsView(db(), t0 - 1h), "oil")->produced, 168'000'000);
  // Later than `since`: not counted.
  EXPECT_EQ(fluid(pumpsView(db(), t0 + 1h), "oil")->produced, 0);
}

UTEST_F(PumpsTest, ModulesComeAndGo) {
  auto r = withRecipes();
  r.modules = {{"aaaa1111", 1, "idle", {}, {}, {}}};
  store(r);
  r.modules = {{"bbbb2222", 1, "idle", {}, {}, {}}};
  const auto events = store(r);
  ASSERT_EQ(events.size(), 2);
  EXPECT_NE(events[0].text.find("bbbb2222"), std::string::npos);
  EXPECT_EQ(events[1].level, "warning");
  EXPECT_NE(events[1].text.find("aaaa1111 is gone"), std::string::npos);
  r.modules = {};
  EXPECT_TRUE(store(r).empty()) << "an empty report says nothing of modules";
}

UTEST_F(PumpsTest, TargetsFromTheClient) {
  store(withRecipes());
  const auto v1 = setPumpTarget(db(), {"oil", 1000, 2, true}, t0);
  ASSERT_TRUE(v1.has_value()) << v1.error();
  EXPECT_EQ(*v1, 1) << "no settings yet: made at version 1";
  const auto v2 = setPumpTarget(db(), {"oil", 2000, 2, false}, t0);
  EXPECT_EQ(v2.value_or(0), 2);
  const auto s = settings();
  ASSERT_TRUE(s);
  EXPECT_EQ(s->batch, 30) << "the defaults";
  EXPECT_EQ(s->targets.at(0), (PumpTarget{"oil", 2000, 2, false}));

  const auto unknown = setPumpTarget(db(), {"lava", 1, 1, true}, t0);
  ASSERT_FALSE(unknown.has_value());
  EXPECT_NE(unknown.error().find("no pump module"), std::string::npos);
  EXPECT_FALSE(setPumpTarget(db(), {"oil", 1, 0, true}, t0).has_value())
      << "priority 0";

  EXPECT_EQ(deletePumpTarget(db(), "oil", t0), 3);
  EXPECT_EQ(deletePumpTarget(db(), "oil", t0), std::nullopt);
  EXPECT_TRUE(settings()->targets.empty());
}

UTEST_F(PumpsTest, GeneralSettings) {
  EXPECT_EQ(setPumpGeneral(db(), 60, 0.9, t0).value_or(0), 1);
  EXPECT_EQ(settings()->batch, 60);
  EXPECT_EQ(settings()->threshold, 0.9);
  EXPECT_FALSE(setPumpGeneral(db(), 0, 0.9, t0).has_value());
  EXPECT_FALSE(setPumpGeneral(db(), 30, 1.5, t0).has_value());
  EXPECT_EQ(settings()->version, 1) << "a refused change changes nothing";
}

UTEST_F(PumpsTest, OldCyclesGo) {
  auto r = withRecipes();
  r.cycles = {{"r1", 1, "a", 30, "done", "", 30, {{"oil", 4, 1}}}};
  store(r, t0);
  r.cycles = {{"r1", 2, "a", 30, "done", "", 30, {{"oil", 4, 1}}}};
  store(r, t0 + 48h);
  EXPECT_EQ(deletePumpCyclesBefore(db(), t0 + 1h), 1);
  EXPECT_EQ(fluid(pumpsView(db(), t0 - 1h), "oil")->produced, 1);
}

}  // namespace
