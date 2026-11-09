#include <userver/formats/json/serialize.hpp>
#include <userver/utest/utest.hpp>

#include "agent/bhc_protocol.hpp"

namespace {

using namespace factoryctl::agent;
using namespace std::chrono_literals;
namespace json = userver::formats::json;

auto error(std::string_view text) -> std::string {
  const auto r = parseBhcPoll(json::FromString(text));
  return r.has_value() ? std::string() : r.error();
}

auto settingsError(std::string_view text) -> std::string {
  const auto r = checkBhcSettings(json::FromString(text));
  return r.has_value() ? std::string() : r.error();
}

// As the agent's link sends it (keys sorted by fc.json).
TEST(BhcProtocol, AFullPoll) {
  const auto r = parseBhcPoll(json::FromString(R"({
    "log": [{"level": "inf", "t": 3.5, "text": "session 4 closed"}],
    "offer": {"floor": 2, "holdAt": 20, "maxHolds": 120, "minSpacetime": 10000},
    "run": "abcd-1", "sessions": [{"ago": 2.5, "blocked": false, "ending": "collapsed",
      "holds": 120, "recipes": 10, "seconds": 200, "session": 4, "spent": 450,
      "unknown": false}],
    "settings": -1,
    "setup": {"coords": {"x": 1, "y": 2, "z": -3}, "name": "bhc@1,2,-3"},
    "state": {"blocked": false, "busy": false, "holds": 120, "low": false, "phase": "idle",
              "session": 4, "spent": 450, "stability": 100, "work": true},
    "supplies": {"collapsers": 2, "seeds": 2, "spacetime": 50000}})"));
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_EQ(r->name, "bhc@1,2,-3");
  EXPECT_EQ(r->coords, (Coords{1, 2, -3}));
  EXPECT_EQ(r->settings, -1);
  ASSERT_TRUE(r->offer);
  EXPECT_EQ(r->status["state"]["phase"].As<std::string>(), "idle");
  EXPECT_EQ(r->status["supplies"]["spacetime"].As<int>(), 50000);
  ASSERT_EQ(r->sessions.size(), 1);
  EXPECT_EQ(r->sessions[0].session, 4);
  EXPECT_EQ(r->sessions[0].recipes, 10);
  EXPECT_EQ(r->sessions[0].ending, "collapsed");
  EXPECT_DOUBLE_EQ(r->sessions[0].ago, 2.5);
  ASSERT_EQ(r->log.size(), 1);
}

TEST(BhcProtocol, AShortPoll) {
  const auto r = parseBhcPoll(json::FromString(
      R"({"setup": {"name": "bhc"}, "run": "r", "settings": 2, "state": [], "sessions": []})"));
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_FALSE(r->coords);
  EXPECT_FALSE(r->offer);
  EXPECT_TRUE(r->status["state"].IsObject())
      << "Lua's empty table is an object here";
  EXPECT_TRUE(r->sessions.empty());
}

TEST(BhcProtocol, PollErrors) {
  EXPECT_NE(error(R"({"run": "r", "settings": 0})").find("setup"),
            std::string::npos);
  EXPECT_NE(error(R"({"setup": {"name": ""}, "run": "r", "settings": 0})")
                .find("setup.name"),
            std::string::npos);
  EXPECT_NE(error(R"({"setup": {"name": "a"}, "run": "r", "settings": 0,
                      "offer": {"floor": 2, "holdAt": 2, "maxHolds": 1, "minSpacetime": 0}})")
                .find("offer: settings.holdAt"),
            std::string::npos);
  EXPECT_NE(error(R"({"setup": {"name": "a"}, "run": "r", "settings": 0,
                      "sessions": [{"session": 1, "seconds": 1, "holds": 0, "spent": 0,
                                    "recipes": 0, "ending": "boom"}]})")
                .find("sessions[0].ending"),
            std::string::npos);
  EXPECT_NE(error(R"({"setup": {"name": "a"}, "run": "r", "settings": 0,
                      "sessions": [{"session": 1, "seconds": -1, "holds": 0, "spent": 0,
                                    "recipes": 0, "ending": "seed"}]})")
                .find("negative"),
            std::string::npos);
  EXPECT_NE(
      error(
          R"({"setup": {"name": "a"}, "run": "r", "settings": 0, "state": 5})")
          .find("state"),
      std::string::npos);
}

TEST(BhcProtocol, Settings) {
  EXPECT_EQ(
      settingsError(
          R"({"holdAt": 20, "floor": 2, "maxHolds": 120, "minSpacetime": 0})"),
      "");
  EXPECT_EQ(
      settingsError(
          R"({"version": 3, "holdAt": 99, "floor": 98, "maxHolds": 600, "minSpacetime": 1})"),
      "");
  EXPECT_NE(
      settingsError(
          R"({"holdAt": 20, "floor": 0, "maxHolds": 1, "minSpacetime": 0})")
          .find("floor"),
      std::string::npos);
  EXPECT_NE(
      settingsError(
          R"({"holdAt": 2, "floor": 2, "maxHolds": 1, "minSpacetime": 0})")
          .find("holdAt"),
      std::string::npos);
  EXPECT_NE(
      settingsError(
          R"({"holdAt": 100, "floor": 2, "maxHolds": 1, "minSpacetime": 0})")
          .find("holdAt"),
      std::string::npos);
  EXPECT_NE(
      settingsError(
          R"({"holdAt": 20, "floor": 2, "maxHolds": 601, "minSpacetime": 0})")
          .find("maxHolds"),
      std::string::npos);
  EXPECT_NE(
      settingsError(
          R"({"holdAt": 20, "floor": 2, "maxHolds": 1, "minSpacetime": -5})")
          .find("minSpacetime"),
      std::string::npos);
  EXPECT_NE(
      settingsError(
          R"({"holdAt": 20.5, "floor": 2, "maxHolds": 1, "minSpacetime": 0})")
          .find("not an integer"),
      std::string::npos);
  EXPECT_NE(settingsError(R"({"holdAt": 20, "floor": 2, "maxHolds": 1})")
                .find("missing"),
            std::string::npos);
  EXPECT_NE(
      settingsError(
          R"({"holdAt": 20, "floor": 2, "maxHolds": 1, "minSpacetime": 0, "holdat": 3})")
          .find("unknown field"),
      std::string::npos)
      << "a typo is refused";
}

TEST(BhcProtocol, Reply) {
  EXPECT_EQ(replyBhcPoll({}), "{poll=30}");
  EXPECT_EQ(replyBhcPoll({.offer = true}), "{poll=30,offer=true}");
  const auto settings = json::FromString(
      R"({"version": 4, "holdAt": 20, "floor": 2, "maxHolds": 120, "minSpacetime": 10000})");
  const auto lua = replyBhcPoll({.settings = settings});
  ASSERT_TRUE(lua.has_value());
  EXPECT_NE(lua->find("version=4"), std::string::npos);
  EXPECT_NE(lua->find("holdAt=20"), std::string::npos);
}

}  // namespace
