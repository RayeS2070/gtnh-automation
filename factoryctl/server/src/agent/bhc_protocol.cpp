#include "agent/bhc_protocol.hpp"

#include <format>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>

#include "agent/fields.hpp"
#include "agent/lua.hpp"

namespace factoryctl::agent {

namespace {

namespace json = userver::formats::json;
using namespace fields;

auto coordsOf(const json::Value& c) -> Result<std::optional<Coords>> {
  if (c.IsMissing() || c.IsNull()) return std::nullopt;
  if (!c.IsObject()) return fail("setup.coords", "not an object");
  Coords coords;
  for (const auto& [field, out] :
       {std::pair{"x", &coords.x}, std::pair{"y", &coords.y},
        std::pair{"z", &coords.z}}) {
    const auto n = integer(c[field], std::format("setup.coords.{}", field));
    if (!n) return std::unexpected(n.error());
    *out = *n;
  }
  return coords;
}

// An object, or the agent's empty table (fc.json writes {} as []).
auto objectOf(const json::Value& v, std::string_view path)
    -> Result<json::Value> {
  if (v.IsMissing() || v.IsNull()) return json::Value{};
  if (v.IsArray() && v.GetSize() == 0) return json::FromString("{}");
  if (!v.IsObject()) return fail(path, "not an object");
  return v;
}

auto session(const json::Value& v, std::string_view path)
    -> Result<BhcSession> {
  if (!v.IsObject()) return fail(path, "not an object");
  BhcSession s;
  for (const auto& [field, out] :
       {std::pair{"session", &s.session}, std::pair{"seconds", &s.seconds},
        std::pair{"holds", &s.holds}, std::pair{"spent", &s.spent},
        std::pair{"recipes", &s.recipes}}) {
    const auto n = integer(v[field], std::format("{}.{}", path, field));
    if (!n) return std::unexpected(n.error());
    if (*n < 0) return fail(std::format("{}.{}", path, field), "negative");
    *out = *n;
  }
  const auto ending = text(v["ending"], std::format("{}.ending", path));
  if (!ending) return std::unexpected(ending.error());
  if (*ending != "collapsed" && *ending != "closed" && *ending != "seed") {
    return fail(std::format("{}.ending", path),
                std::format("unknown '{}'", *ending));
  }
  s.ending = *ending;
  for (const auto& [field, out] :
       {std::pair{"unknown", &s.unknown}, std::pair{"blocked", &s.blocked}}) {
    const auto b = flag(v[field], std::format("{}.{}", path, field), false);
    if (!b) return std::unexpected(b.error());
    *out = *b;
  }
  if (const auto& ago = v["ago"]; !ago.IsMissing()) {
    if (!ago.IsDouble() || ago.As<double>() < 0) {
      return fail(std::format("{}.ago", path), "not a number >= 0");
    }
    s.ago = ago.As<double>();
  }
  return s;
}

}  // namespace

auto checkBhcSettings(const json::Value& settings) -> Result<void> {
  if (!settings.IsObject()) return fail("settings", "not an object");
  for (auto it = settings.begin(); it != settings.end(); ++it) {
    const auto name = it.GetName();
    if (name != "holdAt" && name != "floor" && name != "maxHolds" &&
        name != "minSpacetime" && name != "version") {
      return fail(std::format("settings.{}", name), "unknown field");
    }
  }
  std::int64_t holdAt = 0, floor = 0, maxHolds = 0, minSpacetime = 0;
  for (const auto& [field, out] :
       {std::pair{"holdAt", &holdAt}, std::pair{"floor", &floor},
        std::pair{"maxHolds", &maxHolds},
        std::pair{"minSpacetime", &minSpacetime}}) {
    const auto n = integer(settings[field], std::format("settings.{}", field));
    if (!n) return std::unexpected(n.error());
    *out = *n;
  }
  if (floor < 1 || floor > 98) return fail("settings.floor", "must be 1 to 98");
  if (holdAt <= floor || holdAt > 99) {
    return fail("settings.holdAt", "must be above floor, up to 99");
  }
  if (maxHolds < 0 || maxHolds > kBhcMaxHolds) {
    return fail("settings.maxHolds",
                std::format("must be 0 to {}", kBhcMaxHolds));
  }
  if (minSpacetime < 0) return fail("settings.minSpacetime", "negative");
  return {};
}

auto parseBhcPoll(const json::Value& body) -> Result<BhcReport> {
  if (!body.IsObject()) return fail("bhc poll", "not an object");
  BhcReport r;
  const auto& setup = body["setup"];
  if (!setup.IsObject()) return fail("setup", "not an object");
  const auto name = text(setup["name"], "setup.name");
  if (!name) return std::unexpected(name.error());
  if (name->empty()) return fail("setup.name", "empty");
  r.name = *name;
  auto coords = coordsOf(setup["coords"]);
  if (!coords) return std::unexpected(coords.error());
  r.coords = *coords;
  const auto run = text(body["run"], "run");
  if (!run) return std::unexpected(run.error());
  r.run = *run;
  const auto version = integer(body["settings"], "settings");
  if (!version) return std::unexpected(version.error());
  r.settings = *version;

  if (!body["offer"].IsMissing()) {
    if (auto ok = checkBhcSettings(body["offer"]); !ok) {
      return std::unexpected("offer: " + ok.error());
    }
    r.offer = body["offer"];
  }

  const auto state = objectOf(body["state"], "state");
  if (!state) return std::unexpected(state.error());
  const auto supplies = objectOf(body["supplies"], "supplies");
  if (!supplies) return std::unexpected(supplies.error());
  json::ValueBuilder status;
  status["state"] = *state;
  status["supplies"] = *supplies;
  r.status = status.ExtractValue();

  if (const auto& sessions = body["sessions"]; !sessions.IsMissing()) {
    if (!sessions.IsArray()) return fail("sessions", "not an array");
    for (std::size_t i = 0; i < sessions.GetSize(); ++i) {
      auto s = session(sessions[i], std::format("sessions[{}]", i));
      if (!s) return std::unexpected(s.error());
      r.sessions.push_back(*std::move(s));
    }
  }

  const auto& log = body["log"];
  if (!log.IsMissing()) {
    if (!log.IsArray()) return fail("log", "not an array");
    for (std::size_t i = 0; i < log.GetSize(); ++i) {
      auto l = logLine(log[i], std::format("log[{}]", i));
      if (!l) return std::unexpected(l.error());
      r.log.push_back(*std::move(l));
    }
  }
  return r;
}

auto replyBhcPoll(const BhcReply& reply) -> Result<std::string> {
  json::ValueBuilder b;
  b["poll"] = reply.poll.count();
  if (reply.settings) b["settings"] = *reply.settings;
  if (reply.offer) b["offer"] = true;
  return toLua(b.ExtractValue());
}

}  // namespace factoryctl::agent
