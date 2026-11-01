#include <userver/formats/json/serialize.hpp>
#include <userver/utest/utest.hpp>

#include "agent/pumps_protocol.hpp"
#include "domain/pumps.hpp"

namespace {

using namespace factoryctl::agent;
using namespace factoryctl::domain;
using namespace std::chrono_literals;
namespace json = userver::formats::json;

auto poll(std::string_view text) {
  return parsePumpsPoll(json::FromString(text));
}

auto error(std::string_view text) -> std::string {
  const auto r = poll(text);
  EXPECT_FALSE(r.has_value()) << text;
  return r.has_value() ? "" : r.error();
}

// As the agent's tests/run.lua sends it (keys sorted by fc.json).
TEST(PumpsProtocol, AFullPoll) {
  const auto r = poll(R"({
    "cycles": [{"batch": 30, "duration": 30.6, "module": "aaaa", "result": "done",
                "run": "test-1", "seq": 1,
                "slots": [{"expected": 168000000, "fluid": "helium", "parallel": 4}]},
               {"batch": 30, "message": "did not start", "module": "bbbb",
                "result": "failed", "run": "test-1", "seq": 2, "slots": []}],
    "log": [{"level": "inf", "t": 12.5, "text": "pump module T1 aaaa"}],
    "modules": [{"address": "aaaa", "maxProgress": 600, "progress": 200,
                 "slots": [{"fluid": "helium", "parallel": 4}],
                 "state": "running", "tier": 1},
                {"address": "bbbb", "slots": [], "state": "idle", "tier": 1}],
    "offer": {"batch": 30, "threshold": 0.95, "version": 0,
              "targets": [{"enabled": true, "fluid": "oil", "priority": 1,
                           "target": 10000000000},
                          {"enabled": false, "fluid": "helium", "priority": 2.5,
                           "target": 1000}]},
    "recipes": [{"fluid": "oil", "gas": 3, "label": "Oil", "planet": 4,
                 "rate": 1400000}],
    "run": "test-1", "settings": 0,
    "stock": [{"amount": 0, "fluid": "oil"}, {"amount": 2000000000, "fluid": "neon"}]})");
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_EQ(r->run, "test-1");
  EXPECT_EQ(r->settings, 0);
  ASSERT_TRUE(r->offer);
  EXPECT_EQ(r->offer->batch, 30);
  EXPECT_EQ(r->offer->targets.size(), 2);
  EXPECT_EQ(r->offer->targets[0],
            (PumpTarget{"oil", 10'000'000'000, 1, true}));
  EXPECT_EQ(r->offer->targets[1].priority, 2.5);
  EXPECT_FALSE(r->offer->targets[1].enabled);
  ASSERT_TRUE(r->recipes);
  EXPECT_EQ(r->recipes->at(0), (PumpRecipe{4, 3, "oil", "Oil", 1'400'000}));
  ASSERT_EQ(r->modules.size(), 2);
  EXPECT_EQ(r->modules[0].progress, 200);
  EXPECT_EQ(r->modules[0].slots.at(0), (PumpSlot{"helium", 4, 0}));
  EXPECT_EQ(r->modules[1].progress, std::nullopt);
  ASSERT_EQ(r->cycles.size(), 2);
  EXPECT_EQ(r->cycles[0].slots.at(0).expected, 168'000'000);
  EXPECT_EQ(r->cycles[0].duration, 30.6);
  EXPECT_EQ(r->cycles[1].message, "did not start");
  EXPECT_EQ(r->cycles[1].duration, std::nullopt);
  EXPECT_EQ(r->stock.at(1), (FluidAmount{"neon", 2'000'000'000}));
  EXPECT_EQ(r->log.at(0).text, "pump module T1 aaaa");
}

TEST(PumpsProtocol, AnEmptyPoll) {
  // fc.json sends an empty table as []; no offer, no recipes.
  const auto r = poll(R"({"run": "r", "settings": 4, "modules": [],
                          "cycles": [], "stock": [], "log": []})");
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_EQ(r->settings, 4);
  EXPECT_FALSE(r->offer);
  EXPECT_FALSE(r->recipes);
}

TEST(PumpsProtocol, ErrorsNameTheField) {
  EXPECT_NE(error(R"({"settings": 0})").find("run"), std::string::npos);
  EXPECT_NE(error(R"({"run": "r"})").find("settings"), std::string::npos);
  EXPECT_NE(error(R"({"run": "r", "settings": 0, "cycles": [{"run": "r",
      "seq": 1, "module": "a", "batch": 30, "result": "stuck"}]})")
                .find("cycles[0].result"),
            std::string::npos);
  EXPECT_NE(error(R"({"run": "r", "settings": 0, "modules": [{"address": "a",
      "tier": 1, "state": "idle", "slots": [{"fluid": "oil"}]}]})")
                .find("modules[0].slots[0].parallel"),
            std::string::npos);
  EXPECT_NE(error(R"({"run": "r", "settings": 0, "offer": {"batch": 200,
      "threshold": 0.9, "targets": []}})")
                .find("batch"),
            std::string::npos);
  EXPECT_NE(error(R"({"run": "r", "settings": 0, "offer": {"batch": 30,
      "threshold": 0.9, "targets": [{"fluid": "oil", "target": 1, "priority": 0}]}})")
                .find("priority"),
            std::string::npos);
  EXPECT_NE(error(R"({"run": "r", "settings": 0, "stock": [{"fluid": "oil",
      "amount": 1.5}]})")
                .find("stock[0].amount"),
            std::string::npos);
}

TEST(PumpsProtocol, Reply) {
  EXPECT_EQ(replyPumpsPoll({.poll = 10s}), "{poll=10}");
  EXPECT_EQ(replyPumpsPoll({.poll = 10s, .offer = true}), "{poll=10,offer=true}");
  const PumpsReply withSettings{
      .poll = 10s,
      .settings = PumpSettings{
          .version = 4,
          .batch = 30,
          .threshold = 0.95,
          .targets = {{"oil", 10'000'000'000, 1, true},
                      {"helium", 1000, 2.5, false}}}};
  EXPECT_EQ(replyPumpsPoll(withSettings),
            "{poll=10,settings={version=4,batch=30,threshold=0.95,targets={"
            "{fluid=\"oil\",target=10000000000,priority=1,enabled=true},"
            "{fluid=\"helium\",target=1000,priority=2.5,enabled=false}}}}");
}

TEST(PumpsSettings, Checks) {
  PumpSettings s{.batch = 30, .threshold = 0.95, .targets = {{"oil", 1, 1, true}}};
  EXPECT_TRUE(checkPumpSettings(s).has_value());
  s.batch = 0;
  EXPECT_FALSE(checkPumpSettings(s).has_value());
  s.batch = 128;
  EXPECT_TRUE(checkPumpSettings(s).has_value());
  s.threshold = 0;
  EXPECT_FALSE(checkPumpSettings(s).has_value());
  s.threshold = 1;
  s.targets.push_back({"oil", 2, 1, true});
  EXPECT_NE(checkPumpSettings(s).error().find("twice"), std::string::npos);
  s.targets = {{"oil", -1, 1, true}};
  EXPECT_FALSE(checkPumpSettings(s).has_value());
  s.targets = {{"", 1, 1, true}};
  EXPECT_FALSE(checkPumpSettings(s).has_value());
}

}  // namespace
