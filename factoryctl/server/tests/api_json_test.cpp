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
                     1'366'000,
                     true};
  EXPECT_EQ(
      json::ToString(toJson(row).ExtractValue()),
      R"({"goodsId":2293,"name":"ae2fc:fluid_drop","damage":0,)"
      R"("variant":"sodiumpersulfate","label":"drop of Sodium Persulfate",)"
      R"("amount":1366000,"craftable":true})");
}

TEST(ApiJson, TargetRow) {
  const TargetRow row{{1, {"minecraft:stick", 0, ""}, "Stick", 5},
                      {100, 64, Priority::kLow, false, "note", true}};
  const auto v = toJson(row).ExtractValue();
  EXPECT_EQ(v["goodsId"].As<int>(), 1);
  EXPECT_EQ(v["label"].As<std::string>(), "Stick");
  EXPECT_EQ(v["amount"].As<int>(), 5);
  EXPECT_EQ(v["threshold"].As<int>(), 100);
  EXPECT_EQ(v["batch"].As<int>(), 64);
  EXPECT_EQ(v["priority"].As<std::string>(), "low");
  EXPECT_FALSE(v["enabled"].As<bool>());
  EXPECT_EQ(v["note"].As<std::string>(), "note");
  EXPECT_TRUE(v["fill"].As<bool>());
  EXPECT_TRUE(v["group"].IsNull()) << "no group";
  auto grouped = row;
  grouped.settings.group = 7;
  EXPECT_EQ(toJson(grouped).ExtractValue()["group"].As<int>(), 7);
}

TEST(ApiJson, TargetSettingsGroup) {
  const auto in = settings(R"({"threshold": 1, "batch": 1, "group": 7})");
  ASSERT_TRUE(in.has_value()) << in.error();
  EXPECT_EQ(in->group, 7);
  const auto none = settings(R"({"threshold": 1, "batch": 1, "group": null})");
  ASSERT_TRUE(none.has_value()) << none.error();
  EXPECT_EQ(none->group, std::nullopt);
  const auto bad = settings(R"({"threshold": 1, "batch": 1, "group": "x"})");
  ASSERT_FALSE(bad.has_value());
  EXPECT_NE(bad.error().find("group"), std::string::npos);
}

TEST(ApiJson, Group) {
  const auto v = toJson(TargetGroup{3, "Plasma", 2}).ExtractValue();
  EXPECT_EQ(v["id"].As<int>(), 3);
  EXPECT_EQ(v["name"].As<std::string>(), "Plasma");
  EXPECT_EQ(v["targets"].As<int>(), 2);
}

TEST(ApiJson, GroupName) {
  const auto name = [](std::string_view text) {
    return parseGroupName(json::FromString(text));
  };
  EXPECT_EQ(name(R"({"name": "  Plasma "})").value_or(""), "Plasma") << "trimmed";
  EXPECT_FALSE(name(R"({"name": "   "})").has_value());
  EXPECT_FALSE(name(R"({"name": 1})").has_value());
  EXPECT_FALSE(name(R"({})").has_value());
  EXPECT_FALSE(name(R"({"name": ")" + std::string(101, 'x') + R"("})").has_value());
}

TEST(ApiJson, TargetSettingsFull) {
  const auto s = settings(
      R"({"threshold": 100, "batch": 64, "priority": "low", "enabled": false,
          "note": "assembler", "fill": true})");
  ASSERT_TRUE(s.has_value()) << s.error();
  EXPECT_EQ(
      *s, (TargetSettings{100, 64, Priority::kLow, false, "assembler", true}));
}

TEST(ApiJson, TargetSettingsDefaults) {
  const auto s = settings(R"({"threshold": 0, "batch": 1})");
  ASSERT_TRUE(s.has_value()) << s.error();
  EXPECT_EQ(*s, (TargetSettings{0, 1, Priority::kHigh, true, "", false}));
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
  EXPECT_NE(
      error(R"({"threshold": 1, "batch": 1, "fill": "yes"})").find("fill"),
      std::string::npos);
  EXPECT_NE(error(R"([])").find("object"), std::string::npos);
}

TEST(ApiJson, CpuRow) {
  using namespace std::chrono;
  CpuRow busy{.name = "CPU_014",
              .busy = true,
              .goods = StockRow{1267,
                                {"gregtech:gt.blockcasings10", 11, ""},
                                "Extreme Density Space-Bending Casing",
                                1583,
                                true},
              .remaining = 138,
              .seenAt = sys_days{October / 31 / 2026} + 11h + 500ms,
              .order = 7};
  const auto v = toJson(busy).ExtractValue();
  EXPECT_EQ(v["name"].As<std::string>(), "CPU_014");
  EXPECT_TRUE(v["busy"].As<bool>());
  EXPECT_EQ(v["goods"]["goodsId"].As<int>(), 1267);
  EXPECT_EQ(v["remaining"].As<int>(), 138);
  EXPECT_EQ(v["seenAt"].As<std::string>(), "2026-10-31T11:00:00Z");
  EXPECT_EQ(v["order"].As<int>(), 7);

  const auto idle = toJson(CpuRow{.name = "CPU_001"}).ExtractValue();
  EXPECT_TRUE(idle["goods"].IsNull());
  EXPECT_TRUE(idle["order"].IsNull());
}

TEST(ApiJson, EventRow) {
  using namespace std::chrono;
  EventRow row{.id = 41,
               .at = sys_days{October / 31 / 2026} + 12h + 300ms,
               .level = "warning",
               .source = "agent",
               .text = "probe x: boom",
               .order = 7,
               .goods = GoodsRef{1267,
                                 {"gregtech:gt.blockcasings10", 11, ""},
                                 "Extreme Density Space-Bending Casing"},
               .agentUptime = 123.5};
  const auto v = toJson(row).ExtractValue();
  EXPECT_EQ(v["id"].As<int>(), 41);
  EXPECT_EQ(v["at"].As<std::string>(), "2026-10-31T12:00:00Z");
  EXPECT_EQ(v["level"].As<std::string>(), "warning");
  EXPECT_EQ(v["source"].As<std::string>(), "agent");
  EXPECT_EQ(v["order"].As<int>(), 7);
  EXPECT_EQ(v["goods"]["goodsId"].As<int>(), 1267);
  EXPECT_EQ(v["goods"]["variant"].As<std::string>(), "");
  EXPECT_EQ(v["agentUptime"].As<double>(), 123.5);

  const auto bare = toJson(EventRow{.id = 1, .text = "x"}).ExtractValue();
  EXPECT_TRUE(bare["order"].IsNull());
  EXPECT_TRUE(bare["goods"].IsNull());
  EXPECT_TRUE(bare["agentUptime"].IsNull());
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
