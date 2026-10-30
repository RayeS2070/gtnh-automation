#include <userver/formats/json/serialize.hpp>
#include <userver/utest/utest.hpp>

#include "api/json.hpp"

namespace {

using namespace factoryctl::api;
using namespace factoryctl::db;
using factoryctl::domain::Priority;
namespace json = userver::formats::json;

auto settings(std::string_view text) {
  return parseTargetSettings(json::FromString(text));
}

TEST(ApiJson, StockRow) {
  const StockRow row{2293,
                     {"ae2fc:fluid_drop", 0, "sodiumpersulfate"},
                     "drop of Sodium Persulfate",
                     1'366'000};
  EXPECT_EQ(
      json::ToString(toJson(row).ExtractValue()),
      R"({"goodsId":2293,"name":"ae2fc:fluid_drop","damage":0,)"
      R"("variant":"sodiumpersulfate","label":"drop of Sodium Persulfate",)"
      R"("amount":1366000})");
}

TEST(ApiJson, TargetRow) {
  const TargetRow row{{1, {"minecraft:stick", 0, ""}, "Stick", 5},
                      {100, 64, Priority::kLow, false, "note"}};
  const auto v = toJson(row).ExtractValue();
  EXPECT_EQ(v["goodsId"].As<int>(), 1);
  EXPECT_EQ(v["label"].As<std::string>(), "Stick");
  EXPECT_EQ(v["amount"].As<int>(), 5);
  EXPECT_EQ(v["threshold"].As<int>(), 100);
  EXPECT_EQ(v["batch"].As<int>(), 64);
  EXPECT_EQ(v["priority"].As<std::string>(), "low");
  EXPECT_FALSE(v["enabled"].As<bool>());
  EXPECT_EQ(v["note"].As<std::string>(), "note");
}

TEST(ApiJson, TargetSettingsFull) {
  const auto s = settings(
      R"({"threshold": 100, "batch": 64, "priority": "low", "enabled": false,
          "note": "assembler"})");
  ASSERT_TRUE(s.has_value()) << s.error();
  EXPECT_EQ(*s, (TargetSettings{100, 64, Priority::kLow, false, "assembler"}));
}

TEST(ApiJson, TargetSettingsDefaults) {
  const auto s = settings(R"({"threshold": 0, "batch": 1})");
  ASSERT_TRUE(s.has_value()) << s.error();
  EXPECT_EQ(*s, (TargetSettings{0, 1, Priority::kHigh, true, ""}));
}

TEST(ApiJson, TargetSettingsErrors) {
  const auto error = [](std::string_view text) {
    const auto s = settings(text);
    return s.has_value() ? std::string() : s.error();
  };
  EXPECT_NE(error(R"({"batch": 1})").find("threshold"), std::string::npos);
  EXPECT_NE(error(R"({"threshold": 1})").find("batch"), std::string::npos);
  EXPECT_NE(error(R"({"threshold": "1", "batch": 1})").find("threshold"),
            std::string::npos);
  EXPECT_NE(error(R"({"threshold": 1.5, "batch": 1})").find("threshold"),
            std::string::npos);
  EXPECT_NE(error(R"({"threshold": 1, "batch": 1, "priority": "urgent"})")
                .find("priority"),
            std::string::npos);
  EXPECT_NE(
      error(R"({"threshold": 1, "batch": 1, "enabled": 1})").find("enabled"),
      std::string::npos);
  EXPECT_NE(error(R"([])").find("object"), std::string::npos);
}

TEST(ApiJson, Id) {
  EXPECT_EQ(parseId("2293"), 2293);
  EXPECT_EQ(parseId("0"), std::nullopt);
  EXPECT_EQ(parseId("-1"), std::nullopt);
  EXPECT_EQ(parseId("12x"), std::nullopt);
  EXPECT_EQ(parseId(""), std::nullopt);
}

TEST(ApiJson, Error) {
  EXPECT_EQ(errorJson("no goods \"5\""), R"({"error":"no goods \"5\""})");
}

}  // namespace
