#include <userver/formats/json/serialize.hpp>
#include <userver/utest/utest.hpp>

#include "agent/qft_protocol.hpp"

namespace {

using namespace factoryctl::agent;
using namespace std::chrono_literals;
namespace json = userver::formats::json;

const auto kCatalog = json::FromString(R"([
  {"id": "glue", "name": "Glue", "trigger": "deficit", "tier": 1, "catalysts": 1,
   "inputs": [{"kind": "fluid", "label": "Water"}],
   "outputs": [{"circuit": 1, "kind": "fluid", "label": "Glue", "target": 100, "priority": 2},
               {"circuit": 2, "kind": "fluid", "label": "Waste"}]},
  {"id": "plat", "name": "Plat", "trigger": "surplus", "tier": 1, "catalysts": 1,
   "inputs": [], "outputs": [{"circuit": 1, "kind": "fluid", "label": "Pt", "weight": 1}]}])");

auto check(std::string_view text) {
  return checkQftSettings(json::FromString(text), kCatalog);
}

// As the agent's tests send it (keys sorted by fc.json).
TEST(QftProtocol, AFullPoll) {
  const auto r = parseQftPoll(json::FromString(R"({
    "catalog": [{"id": "glue", "outputs": [{"circuit": 1}]}],
    "log": [{"level": "inf", "t": 3.5, "text": "started glue"}],
    "offer": {"mode": "auto", "recipes": [{"id": "glue", "outputs": [{"circuit": 1,
              "priority": 2, "target": 100}]}], "version": 0},
    "plasma": {"fermium": 900},
    "recipes": [{"done": false, "id": "glue", "low": true, "outputs": [{"amount": 10,
                 "circuit": 1, "target": 100}], "trigger": "deficit", "urgency": 0.99}],
    "run": "test-1", "settings": 0,
    "setup": {"coords": {"x": 1, "y": 2, "z": 3}, "name": "qft-main"},
    "state": {"cycleSeconds": 6, "fermium": true, "mode": "auto", "phase": "running",
              "recipe": "glue", "want": "glue"}})"));
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_EQ(r->name, "qft-main");
  EXPECT_EQ(r->coords, (Coords{1, 2, 3}));
  EXPECT_EQ(r->run, "test-1");
  ASSERT_TRUE(r->offer);
  ASSERT_TRUE(r->catalog);
  EXPECT_EQ(r->status["state"]["recipe"].As<std::string>(), "glue");
  EXPECT_EQ(r->status["recipes"][0]["outputs"][0]["amount"].As<int>(), 10);
  EXPECT_EQ(r->status["plasma"]["fermium"].As<int>(), 900);
  ASSERT_EQ(r->log.size(), 1);
}

TEST(QftProtocol, AShortPoll) {
  const auto r = parseQftPoll(json::FromString(
      R"({"setup": {"name": "qft@1,2,3"}, "run": "r", "settings": 2, "log": []})"));
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_FALSE(r->coords);
  EXPECT_FALSE(r->offer);
  EXPECT_TRUE(r->status["recipes"].IsArray());
}

TEST(QftProtocol, PollErrors) {
  const auto error = [](std::string_view text) {
    const auto r = parseQftPoll(json::FromString(text));
    return r.has_value() ? std::string() : r.error();
  };
  EXPECT_NE(error(R"({"run": "r", "settings": 0})").find("setup"), std::string::npos);
  EXPECT_NE(error(R"({"setup": {"name": ""}, "run": "r", "settings": 0})").find("setup.name"),
            std::string::npos);
  EXPECT_NE(error(R"({"setup": {"name": "a", "coords": {"x": 1}}, "run": "r", "settings": 0})")
                .find("coords.y"),
            std::string::npos);
  EXPECT_NE(error(R"({"setup": {"name": "a"}, "run": "r", "settings": 0,
                      "offer": {"mode": "never"}})")
                .find("mode"),
            std::string::npos);
}

TEST(QftProtocol, Settings) {
  EXPECT_TRUE(check(R"({"mode": "auto"})").has_value());
  EXPECT_TRUE(check(R"({"mode": "deficit", "recipes": [{"id": "glue", "outputs": [
      {"circuit": 1, "target": 500, "priority": 1}, {"circuit": 2}]}]})")
                  .has_value());
  EXPECT_NE(check(R"({"mode": "auto", "recipes": [{"id": "gone"}]})").error().find("no recipe"),
            std::string::npos);
  EXPECT_NE(check(R"({"mode": "auto", "recipes": [{"id": "glue", "outputs": [{"circuit": 9}]}]})")
                .error()
                .find("no output 9"),
            std::string::npos);
  EXPECT_NE(check(R"({"mode": "auto", "recipes": [{"id": "glue", "outputs": [
      {"circuit": 1, "target": 0}]}]})")
                .error()
                .find("> 0"),
            std::string::npos);
  EXPECT_NE(check(R"({"mode": "auto", "recipes": [{"id": "glue", "outputs": [
      {"circuit": 1, "taget": 5}]}]})")
                .error()
                .find("unknown field"),
            std::string::npos)
      << "a typo is refused";
  EXPECT_FALSE(check(R"({"mode": "auto", "recipes": [{"id": "glue"}, {"id": "glue"}]})")
                   .has_value());
  EXPECT_TRUE(check(R"({"mode": "auto", "recipes": [{"id": "glue", "enabled": false}]})")
                  .has_value());
  EXPECT_NE(check(R"({"mode": "auto", "recipes": [{"id": "glue", "enabled": "no"}]})")
                .error()
                .find("enabled"),
            std::string::npos);
  // Without a catalog the ids are not checked.
  EXPECT_TRUE(checkQftSettings(json::FromString(R"({"mode": "auto", "recipes": [{"id": "x"}]})"),
                               json::Value{})
                  .has_value());
}

TEST(QftProtocol, MergeCatalog) {
  const auto settings = json::FromString(
      R"({"mode": "auto", "recipes": [{"id": "glue", "outputs": [{"circuit": 1, "target": 5}]}]})");
  const auto merged = mergeCatalog(settings, kCatalog);
  ASSERT_TRUE(merged);
  ASSERT_EQ((*merged)["recipes"].GetSize(), 2);
  EXPECT_EQ((*merged)["recipes"][0]["outputs"][0]["target"].As<int>(), 5) << "kept";
  EXPECT_EQ((*merged)["recipes"][1]["id"].As<std::string>(), "plat");
  EXPECT_EQ((*merged)["recipes"][1]["outputs"][0]["weight"].As<int>(), 1) << "catalog values";
  EXPECT_FALSE(mergeCatalog(*merged, kCatalog)) << "nothing missing";
}

TEST(QftProtocol, Reply) {
  EXPECT_EQ(replyQftPoll({.poll = 10s}), "{poll=10}");
  EXPECT_EQ(replyQftPoll({.poll = 10s, .offer = true}), "{poll=10,offer=true}");
  const auto settings = json::FromString(
      R"({"version": 4, "mode": "auto", "recipes": [{"id": "glue", "outputs": [{"circuit": 1, "target": 5}]}]})");
  EXPECT_EQ(replyQftPoll({.poll = 10s, .settings = settings}),
            "{poll=10,settings={version=4,mode=\"auto\",recipes={{id=\"glue\",outputs={"
            "{circuit=1,target=5}}}}}}");
}

}  // namespace
