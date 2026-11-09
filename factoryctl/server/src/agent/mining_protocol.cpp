#include "agent/mining_protocol.hpp"

#include <format>
#include <userver/formats/json/value_builder.hpp>
#include <utility>

#include "agent/fields.hpp"
#include "agent/lua.hpp"

namespace factoryctl::agent {

namespace {

namespace json = userver::formats::json;
using namespace fields;

constexpr int kMaxSlots = 63;  // 18 + 9 per capacity card, 5 cards

auto list(const json::Value& v, std::string_view path) -> Result<void> {
  if (!v.IsMissing() && !v.IsArray()) return fail(path, "not an array");
  return {};
}

auto inRange(const json::Value& v, std::string_view path, std::int64_t min,
             std::int64_t max) -> Result<int> {
  const auto n = integer(v, path);
  if (!n) return std::unexpected(n.error());
  if (*n < min || *n > max) {
    return fail(path, std::format("not in {}..{}", min, max));
  }
  return static_cast<int>(*n);
}

auto parseModule(const json::Value& m, std::string_view path)
    -> Result<MiningModuleReport> {
  if (!m.IsObject()) return fail(path, "not an object");
  MiningModuleReport out;
  for (const auto [field, coord] : {std::pair{"x", &Coords::x},
                                    std::pair{"y", &Coords::y},
                                    std::pair{"z", &Coords::z}}) {
    const auto v = integer(m[field], std::format("{}.{}", path, field));
    if (!v) return std::unexpected(v.error());
    out.at.*coord = *v;
  }
  const auto tier = inRange(m["tier"], std::format("{}.tier", path), 1, 3);
  if (!tier) return std::unexpected(tier.error());
  out.tier = *tier;
  const auto state = text(m["state"], std::format("{}.state", path));
  if (!state) return std::unexpected(state.error());
  out.state = *state;
  const auto active = flag(m["active"], std::format("{}.active", path), false);
  if (!active) return std::unexpected(active.error());
  out.active = *active;
  if (!m["drone"].IsMissing()) {
    const auto d = inRange(m["drone"], std::format("{}.drone", path), 0,
                           core::kDroneTierCount - 1);
    if (!d) return std::unexpected(d.error());
    out.drone = *d;
  }
  if (!m["assignment"].IsMissing()) {
    const auto a = text(m["assignment"], std::format("{}.assignment", path));
    if (!a) return std::unexpected(a.error());
    out.assignment = *a;
  }
  if (!m["error"].IsMissing()) {
    const auto e = text(m["error"], std::format("{}.error", path));
    if (!e) return std::unexpected(e.error());
    out.error = *e;
  }
  return out;
}

auto itemLua(const domain::ItemKey& key) -> json::ValueBuilder {
  json::ValueBuilder b;
  b["name"] = key.name;
  b["damage"] = key.damage;
  return b;
}

}  // namespace

auto moduleId(const Coords& at) -> std::string {
  return std::format("{},{},{}", at.x, at.y, at.z);
}

auto settingId(const domain::MiningSetting& s) -> std::string {
  return std::format("{}/{}/{}/{}", s.droneTier, s.distance, s.plasmaTier,
                     s.parallel);
}

auto parseMiningPoll(const json::Value& body) -> Result<MiningReport> {
  if (!body.IsObject()) return fail("body", "not an object");
  MiningReport r;
  const auto name = text(body["setup"], "setup");
  if (!name) return std::unexpected(name.error());
  if (name->empty() || name->size() > 100) return fail("setup", "1..100 bytes");
  r.name = *name;
  const auto run = text(body["run"], "run");
  if (!run) return std::unexpected(run.error());
  r.run = *run;

  for (const auto* field : {"modules", "pool", "buffer", "log"}) {
    if (auto ok = list(body[field], field); !ok) return std::unexpected(ok.error());
  }
  // A list left out is empty (a missing value has no size).
  const auto size = [](const json::Value& v) {
    return v.IsArray() ? v.GetSize() : std::size_t{0};
  };
  const auto& modules = body["modules"];
  for (std::size_t i = 0; i < size(modules); ++i) {
    auto m = parseModule(modules[i], std::format("modules[{}]", i));
    if (!m) return std::unexpected(m.error());
    for (const auto& seen : r.modules) {
      if (seen.at == m->at) {
        return fail(std::format("modules[{}]", i), "the same x, y, z twice");
      }
    }
    r.modules.push_back(std::move(*m));
  }
  const auto& pool = body["pool"];
  for (std::size_t i = 0; i < size(pool); ++i) {
    const auto path = std::format("pool[{}]", i);
    const auto tier = inRange(pool[i]["tier"], path + ".tier", 0,
                              core::kDroneTierCount - 1);
    if (!tier) return std::unexpected(tier.error());
    const auto count = inRange(pool[i]["count"], path + ".count", 0, 1'000'000);
    if (!count) return std::unexpected(count.error());
    r.pool[*tier] += *count;
  }
  const auto& buffer = body["buffer"];
  for (std::size_t i = 0; i < size(buffer); ++i) {
    const auto path = std::format("buffer[{}]", i);
    const auto& b = buffer[i];
    const auto n = text(b["name"], path + ".name");
    if (!n) return std::unexpected(n.error());
    const auto damage = inRange(b["damage"], path + ".damage", 0, 32767);
    if (!damage) return std::unexpected(damage.error());
    const auto size = integer(b["size"], path + ".size");
    if (!size) return std::unexpected(size.error());
    if (*size < 0) return fail(path + ".size", "negative");
    r.buffer[{.name = *n, .damage = *damage}] += static_cast<double>(*size);
  }
  if (const auto& routing = body["routing"]; !routing.IsMissing()) {
    if (!routing.IsObject()) return fail("routing", "not an object");
    if (!routing["slots"].IsMissing()) {
      const auto slots = inRange(routing["slots"], "routing.slots", 0, kMaxSlots);
      if (!slots) return std::unexpected(slots.error());
      r.slots = *slots;
    }
  }
  const auto& log = body["log"];
  for (std::size_t i = 0; i < size(log); ++i) {
    auto l = logLine(log[i], std::format("log[{}]", i));
    if (!l) return std::unexpected(l.error());
    r.log.push_back(std::move(*l));
  }
  json::ValueBuilder status;
  status["modules"] = modules.IsMissing() ? json::ValueBuilder(json::Type::kArray)
                                          : json::ValueBuilder(modules);
  status["pool"] = pool.IsMissing() ? json::ValueBuilder(json::Type::kArray)
                                    : json::ValueBuilder(pool);
  status["buffer"] = buffer.IsMissing() ? json::ValueBuilder(json::Type::kArray)
                                        : json::ValueBuilder(buffer);
  if (r.slots) status["slots"] = *r.slots;
  r.status = status.ExtractValue();
  return r;
}

auto replyMiningPoll(const MiningReply& reply,
                     const domain::MiningCatalog& catalog) -> Result<std::string> {
  json::ValueBuilder b;
  b["poll"] = reply.poll.count();
  json::ValueBuilder modules(json::Type::kArray);
  for (const auto& m : reply.modules) {
    json::ValueBuilder e;
    e["x"] = m.at.x;
    e["y"] = m.at.y;
    e["z"] = m.at.z;
    if (!m.setting) {
      e["idle"] = true;
    } else {
      const auto& s = *m.setting;
      if (s.droneTier < 0 ||
          std::cmp_greater_equal(s.droneTier, catalog.drones.size()) ||
          s.plasmaTier < 1 ||
          std::cmp_greater(s.plasmaTier, catalog.plasmas.size())) {
        return fail("reply", std::format("no drone or plasma for {}", settingId(s)));
      }
      const auto& d = catalog.drones[static_cast<std::size_t>(s.droneTier)];
      e["id"] = settingId(s);
      e["droneTier"] = s.droneTier;
      e["drone"] = itemLua(d.drone.key);
      e["tip"] = itemLua(d.tip.key);
      e["rod"] = itemLua(d.rod.key);
      e["plasma"] =
          catalog.plasmas[static_cast<std::size_t>(s.plasmaTier - 1)].fluid;
      e["distance"] = s.distance;
      e["parallel"] = s.parallel;
    }
    modules.PushBack(std::move(e));
  }
  b["modules"] = std::move(modules);
  json::ValueBuilder keep(json::Type::kArray);
  for (const auto& k : reply.keep) keep.PushBack(itemLua(k));
  b["keep"] = std::move(keep);
  b["buffer"]["limit"] = static_cast<std::int64_t>(reply.bufferLimit);
  return toLua(b.ExtractValue());
}

}  // namespace factoryctl::agent
