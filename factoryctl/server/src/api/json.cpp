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
  e["group"] = s.group ? json::ValueBuilder{*s.group} : json::ValueBuilder{};
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
  if (const auto& v = body["group"]; !v.IsMissing() && !v.IsNull()) {
    if (!v.IsInt64()) return fail("group", "not an id");
    s.group = v.As<std::int64_t>();
  }
  return s;
}

auto toJson(const db::TargetGroup& g) -> json::ValueBuilder {
  json::ValueBuilder e;
  e["id"] = g.id;
  e["name"] = g.name;
  e["targets"] = g.targets;
  return e;
}

auto parseGroupName(const json::Value& body)
    -> std::expected<std::string, std::string> {
  constexpr std::size_t kMaxName = 100;
  if (!body.IsObject()) return fail("group", "not an object");
  const auto& v = body["name"];
  if (v.IsMissing()) return fail("name", "missing");
  if (!v.IsString()) return fail("name", "not a string");
  const auto text = v.As<std::string>();
  const auto first = text.find_first_not_of(" \t");
  if (first == std::string::npos) return fail("name", "empty");
  auto name = text.substr(first, text.find_last_not_of(" \t") - first + 1);
  // Bytes, not characters: a bound, not a rule people see often.
  if (name.size() > kMaxName) return fail("name", "longer than 100");
  return name;
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

auto toJson(const db::EventRow& row) -> json::ValueBuilder {
  json::ValueBuilder e;
  e["id"] = row.id;
  e["at"] = utc(row.at);
  e["level"] = row.level;
  e["source"] = row.source;
  e["text"] = row.text;
  e["order"] = row.order ? json::ValueBuilder{*row.order} : json::ValueBuilder{};
  if (row.goods) {
    json::ValueBuilder g;
    g["goodsId"] = row.goods->id;
    g["name"] = row.goods->key.name;
    g["damage"] = row.goods->key.damage;
    g["variant"] = row.goods->key.variant;
    g["label"] = row.goods->label;
    e["goods"] = std::move(g);
  } else {
    e["goods"] = json::ValueBuilder{};
  }
  e["agentUptime"] = row.agentUptime ? json::ValueBuilder{*row.agentUptime}
                                     : json::ValueBuilder{};
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

auto toJson(const db::PumpsView& v) -> json::ValueBuilder {
  using userver::formats::common::Type;
  json::ValueBuilder b;
  if (v.settings) {
    b["settings"]["version"] = v.settings->version;
    b["settings"]["batch"] = v.settings->batch;
    b["settings"]["threshold"] = v.settings->threshold;
  } else {
    b["settings"] = json::ValueBuilder{};
  }
  b["failed"] = v.failed;
  json::ValueBuilder modules(Type::kArray);
  for (const auto& row : v.modules) {
    const auto& m = row.module;
    json::ValueBuilder e;
    e["address"] = m.address;
    e["tier"] = m.tier;
    e["state"] = m.state;
    e["progress"] = m.progress ? json::ValueBuilder{*m.progress} : json::ValueBuilder{};
    e["maxProgress"] =
        m.maxProgress ? json::ValueBuilder{*m.maxProgress} : json::ValueBuilder{};
    e["seenAt"] = utc(row.seenAt);
    json::ValueBuilder slots(Type::kArray);
    for (const auto& s : m.slots) {
      json::ValueBuilder one;
      one["fluid"] = s.fluid;
      one["parallel"] = s.parallel;
      slots.PushBack(std::move(one));
    }
    e["slots"] = std::move(slots);
    modules.PushBack(std::move(e));
  }
  b["modules"] = std::move(modules);
  json::ValueBuilder fluids(Type::kArray);
  for (const auto& f : v.fluids) {
    json::ValueBuilder e;
    e["fluid"] = f.recipe.fluid;
    e["label"] = f.recipe.label;
    e["planet"] = f.recipe.planet;
    e["gas"] = f.recipe.gas;
    e["rate"] = f.recipe.rate;
    e["goodsId"] = f.goodsId ? json::ValueBuilder{*f.goodsId} : json::ValueBuilder{};
    e["amount"] = f.amount ? json::ValueBuilder{*f.amount} : json::ValueBuilder{};
    e["inFlight"] = f.inFlight;
    e["produced"] = f.produced;
    if (f.target) {
      e["target"]["target"] = f.target->target;
      e["target"]["priority"] = f.target->priority;
      e["target"]["enabled"] = f.target->enabled;
    } else {
      e["target"] = json::ValueBuilder{};
    }
    fluids.PushBack(std::move(e));
  }
  b["fluids"] = std::move(fluids);
  return b;
}

auto parsePumpGeneral(const json::Value& body)
    -> std::expected<PumpGeneral, std::string> {
  if (!body.IsObject()) return fail("settings", "not an object");
  PumpGeneral g;
  const auto& batch = body["batch"];
  if (batch.IsMissing()) return fail("batch", "missing");
  if (!batch.IsInt()) return fail("batch", "not an integer");
  g.batch = batch.As<std::int32_t>();
  const auto& threshold = body["threshold"];
  if (threshold.IsMissing()) return fail("threshold", "missing");
  if (!threshold.IsDouble()) return fail("threshold", "not a number");
  g.threshold = threshold.As<double>();
  return g;
}

auto parsePumpTarget(const json::Value& body, std::string fluid)
    -> std::expected<domain::PumpTarget, std::string> {
  if (!body.IsObject()) return fail("target", "not an object");
  domain::PumpTarget t{.fluid = std::move(fluid)};
  const auto& target = body["target"];
  if (target.IsMissing()) return fail("target", "missing");
  if (!target.IsInt64()) return fail("target", "not an integer");
  t.target = target.As<std::int64_t>();
  if (const auto& v = body["priority"]; !v.IsMissing()) {
    if (!v.IsDouble()) return fail("priority", "not a number");
    t.priority = v.As<double>();
  }
  if (const auto& v = body["enabled"]; !v.IsMissing()) {
    if (!v.IsBool()) return fail("enabled", "not a boolean");
    t.enabled = v.As<bool>();
  }
  return t;
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
