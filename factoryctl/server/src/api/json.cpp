#include "api/json.hpp"

#include <charconv>
#include <chrono>
#include <format>
#include <userver/formats/json/serialize.hpp>

namespace factoryctl::api {

namespace {

namespace json = userver::formats::json;

auto fail(std::string_view field, std::string_view what)
    -> std::unexpected<std::string> {
  return std::unexpected(std::format("{}: {}", field, what));
}

auto utc(domain::TimePoint t) -> std::string {
  return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(t));
}

}  // namespace

auto toJson(const db::StockRow& row) -> json::ValueBuilder {
  json::ValueBuilder e;
  e["goodsId"] = row.goodsId;
  e["name"] = row.key.name;
  e["damage"] = row.key.damage;
  e["variant"] = row.key.variant;
  e["label"] = row.label;
  e["amount"] = row.amount;
  e["craftable"] = row.craftable;
  return e;
}

auto toJson(const db::TargetRow& row) -> json::ValueBuilder {
  auto e = toJson(row.goods);
  const auto& s = row.settings;
  e["threshold"] = s.threshold;
  e["batch"] = s.batch;
  e["priority"] = s.priority == domain::Priority::kLow ? "low" : "high";
  e["enabled"] = s.enabled;
  e["note"] = s.note;
  e["fill"] = s.fill;
  return e;
}

auto parseTargetSettings(const json::Value& body)
    -> std::expected<db::TargetSettings, std::string> {
  if (!body.IsObject()) return fail("target", "not an object");
  db::TargetSettings s;
  for (const auto& [field, out] :
       {std::pair{"threshold", &s.threshold}, std::pair{"batch", &s.batch}}) {
    const auto& v = body[field];
    if (v.IsMissing()) return fail(field, "missing");
    if (!v.IsInt64()) return fail(field, "not an integer");
    *out = v.As<std::int64_t>();
  }
  if (const auto& v = body["priority"]; !v.IsMissing()) {
    const auto p = v.IsString() ? v.As<std::string>() : std::string();
    if (p == "low") {
      s.priority = domain::Priority::kLow;
    } else if (p != "high") {
      return fail("priority", "must be \"high\" or \"low\"");
    }
  }
  if (const auto& v = body["enabled"]; !v.IsMissing()) {
    if (!v.IsBool()) return fail("enabled", "not a boolean");
    s.enabled = v.As<bool>();
  }
  if (const auto& v = body["fill"]; !v.IsMissing()) {
    if (!v.IsBool()) return fail("fill", "not a boolean");
    s.fill = v.As<bool>();
  }
  if (const auto& v = body["note"]; !v.IsMissing()) {
    if (!v.IsString()) return fail("note", "not a string");
    s.note = v.As<std::string>();
  }
  return s;
}

auto toJson(const db::OrderRow& row) -> json::ValueBuilder {
  json::ValueBuilder e;
  e["id"] = row.id;
  e["goods"] = toJson(row.goods);
  e["amount"] = row.amount;
  e["status"] = row.status;
  e["source"] = row.source;
  e["requestedBy"] = row.requestedBy;
  e["createdAt"] = utc(row.createdAt);
  e["changedAt"] = utc(row.changedAt);
  e["message"] = row.message;
  e["cpu"] = row.cpu;
  e["remaining"] = row.remaining;
  e["cancelRequested"] = row.cancelRequested;
  return e;
}

auto toJson(const db::CpuRow& row) -> json::ValueBuilder {
  json::ValueBuilder e;
  e["name"] = row.name;
  e["busy"] = row.busy;
  e["goods"] = row.goods ? toJson(*row.goods) : json::ValueBuilder{};
  e["remaining"] = row.remaining;
  e["seenAt"] = utc(row.seenAt);
  e["order"] = row.order ? json::ValueBuilder{*row.order} : json::ValueBuilder{};
  return e;
}

auto parseOrderRequest(const json::Value& body)
    -> std::expected<OrderRequest, std::string> {
  if (!body.IsObject()) return fail("order", "not an object");
  OrderRequest r;
  for (const auto& [field, out] :
       {std::pair{"goodsId", &r.goodsId}, std::pair{"amount", &r.amount}}) {
    const auto& v = body[field];
    if (v.IsMissing()) return fail(field, "missing");
    if (!v.IsInt64()) return fail(field, "not an integer");
    *out = v.As<std::int64_t>();
  }
  if (const auto& v = body["requestedBy"]; !v.IsMissing()) {
    if (!v.IsString()) return fail("requestedBy", "not a string");
    r.requestedBy = v.As<std::string>();
  }
  return r;
}

auto parseId(std::string_view text) -> std::optional<std::int64_t> {
  std::int64_t id = 0;
  const auto [end, ec] =
      std::from_chars(text.data(), text.data() + text.size(), id);
  if (text.empty() || ec != std::errc{} || end != text.data() + text.size() ||
      id <= 0) {
    return std::nullopt;
  }
  return id;
}

auto errorJson(std::string_view message) -> std::string {
  json::ValueBuilder b;
  b["error"] = std::string(message);
  return json::ToString(b.ExtractValue());
}

}  // namespace factoryctl::api
