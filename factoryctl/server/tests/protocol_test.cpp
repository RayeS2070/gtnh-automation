#include "agent/protocol.hpp"

#include <userver/formats/json/serialize.hpp>
#include <userver/utest/utest.hpp>

namespace {

using factoryctl::agent::parseScan;
using factoryctl::domain::ScannedStack;
namespace json = userver::formats::json;

auto scan(std::string_view text) { return parseScan(json::FromString(text)); }

auto error(std::string_view text) -> std::string {
  const auto r = scan(text);
  EXPECT_FALSE(r.has_value()) << text;
  return r.has_value() ? "" : r.error();
}

TEST(Protocol, ScanBatch) {
  const auto r = scan(R"({"scan": 42, "last": true, "items": [
      {"name": "minecraft:stick", "damage": 0, "label": "Stick", "size": 5},
      {"name": "ae2fc:fluid_drop", "damage": 0, "label": "drop of Water",
       "size": 4503599627370495, "fluid": "water"}]})");
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_EQ(r->scan, 42);
  EXPECT_TRUE(r->last);
  ASSERT_EQ(r->stacks.size(), 2);
  EXPECT_EQ(r->stacks[0].name, "minecraft:stick");
  EXPECT_EQ(r->stacks[0].label, "Stick");
  EXPECT_EQ(r->stacks[0].size, 5);
  EXPECT_EQ(r->stacks[0].fluid, "");
  EXPECT_EQ(r->stacks[1].fluid, "water");
  EXPECT_EQ(r->stacks[1].size, 4'503'599'627'370'495);
}

TEST(Protocol, ScanDefaults) {
  const auto r = scan(R"({"scan": 1, "items": [
      {"name": "minecraft:stick", "label": "Stick", "size": 0}]})");
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_FALSE(r->last);
  EXPECT_EQ(r->stacks[0].damage, 0);
}

TEST(Protocol, ScanCraftable) {
  const auto r = scan(R"({"scan": 1, "items": [
      {"name": "a:b", "label": "B", "size": 900, "craftable": false},
      {"name": "a:c", "label": "C", "size": 0, "craftable": true},
      {"name": "a:d", "label": "D", "size": 0}]})");
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_FALSE(r->stacks[0].craftable);
  EXPECT_TRUE(r->stacks[1].craftable);
  EXPECT_TRUE(r->stacks[2].craftable) << "an agent that does not tell";
  EXPECT_NE(error(R"({"scan": 1, "items": [
      {"name": "a:b", "label": "B", "size": 1, "craftable": 1}]})")
                .find("items[0].craftable"),
            std::string::npos);
}

TEST(Protocol, ScanWithoutItems) {
  // The last batch may be empty: it only closes the scan.
  const auto r = scan(R"({"scan": 1, "last": true, "items": []})");
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_TRUE(r->stacks.empty());
}

TEST(Protocol, ScanSizeAsFloat) {
  // OC numbers may come as floats (Lua 5.3 tostring of 1e3 is "1000.0").
  const auto r = scan(R"({"scan": 1, "items": [
      {"name": "a:b", "label": "B", "size": 1000.0}]})");
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_EQ(r->stacks[0].size, 1000);
}

TEST(Protocol, ScanErrorsNameTheField) {
  EXPECT_NE(error(R"({"items": []})").find("scan"), std::string::npos);
  EXPECT_NE(error(R"({"scan": "x", "items": []})").find("scan"),
            std::string::npos);
  EXPECT_NE(error(R"({"scan": 1})").find("items"), std::string::npos);
  EXPECT_NE(error(R"({"scan": 1, "items": {}})").find("items"),
            std::string::npos);
  EXPECT_NE(error(R"({"scan": 1, "last": 1, "items": []})").find("last"),
            std::string::npos);
  EXPECT_NE(error(R"({"scan": 1, "items": [{"label": "B", "size": 1}]})")
                .find("name"),
            std::string::npos);
  EXPECT_NE(
      error(R"({"scan": 1, "items": [{"name": "", "label": "B", "size": 1}]})")
          .find("name"),
      std::string::npos);
  EXPECT_NE(error(R"({"scan": 1, "items": [{"name": "a:b", "size": 1}]})")
                .find("label"),
            std::string::npos);
  EXPECT_NE(error(R"({"scan": 1, "items": [{"name": "a:b", "label": "B"}]})")
                .find("size"),
            std::string::npos);
  EXPECT_NE(
      error(
          R"({"scan": 1, "items": [{"name": "a:b", "label": "B", "size": -1}]})")
          .find("size"),
      std::string::npos);
  EXPECT_NE(
      error(
          R"({"scan": 1, "items": [{"name": "a:b", "label": "B", "size": 1.5}]})")
          .find("size"),
      std::string::npos);
  EXPECT_NE(error(R"({"scan": 1, "items": [1]})").find("items[0]"),
            std::string::npos);
}

TEST(Protocol, ScanNotAnObject) {
  EXPECT_FALSE(scan("[]").has_value());
  EXPECT_FALSE(scan("null").has_value());
}

}  // namespace
