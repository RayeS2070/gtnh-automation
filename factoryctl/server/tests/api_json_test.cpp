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

TEST(ApiJson, OrderRow) {
  using namespace std::chrono;
  OrderRow row;
  row.id = 1;
  row.goods = {1267,
               {"gregtech:gt.blockcasings10", 11, ""},
               "Extreme Density Space-Bending Casing",
               1583};
  row.amount = 128;
  row.status = "crafting";
  row.source = "manual";
  row.requestedBy = "Pavel";
  row.createdAt = sys_days{October / 30 / 2026} + 6h + 40min + 20s + 316ms;
  row.changedAt = row.createdAt + 16s;
  row.cpu = "CPU_022";
  row.remaining = 115;
  const auto v = toJson(row).ExtractValue();
  EXPECT_EQ(v["id"].As<int>(), 1);
  EXPECT_EQ(v["goods"]["label"].As<std::string>(),
            "Extreme Density Space-Bending Casing");
  EXPECT_EQ(v["goods"]["amount"].As<int>(), 1583);
  EXPECT_EQ(v["status"].As<std::string>(), "crafting");
  EXPECT_EQ(v["createdAt"].As<std::string>(), "2026-10-30T06:40:20Z");
  EXPECT_EQ(v["changedAt"].As<std::string>(), "2026-10-30T06:40:36Z");
  EXPECT_EQ(v["cpu"].As<std::string>(), "CPU_022");
  EXPECT_EQ(v["remaining"].As<int>(), 115);
  EXPECT_FALSE(v["cancelRequested"].As<bool>());
}

TEST(ApiJson, OrderRequest) {
  const auto r = parseOrderRequest(json::FromString(
      R"({"goodsId": 1267, "amount": 128, "requestedBy": "Pavel"})"));
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_EQ(r->goodsId, 1267);
  EXPECT_EQ(r->amount, 128);
  EXPECT_EQ(r->requestedBy, "Pavel");
  const auto anonymous =
      parseOrderRequest(json::FromString(R"({"goodsId": 1, "amount": 1})"));
  ASSERT_TRUE(anonymous.has_value());
  EXPECT_EQ(anonymous->requestedBy, "");
}

TEST(ApiJson, OrderRequestErrors) {
  const auto error = [](std::string_view text) {
    const auto r = parseOrderRequest(json::FromString(text));
    return r.has_value() ? std::string() : r.error();
  };
  EXPECT_NE(error(R"({"amount": 1})").find("goodsId"), std::string::npos);
  EXPECT_NE(error(R"({"goodsId": 1})").find("amount"), std::string::npos);
  EXPECT_NE(error(R"({"goodsId": 1, "amount": "128"})").find("amount"),
            std::string::npos);
  EXPECT_NE(error(R"({"goodsId": 1, "amount": 1, "requestedBy": 5})")
                .find("requestedBy"),
            std::string::npos);
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
