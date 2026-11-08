#include "agent/qft_protocol.hpp"

#include <format>
#include <map>
#include <set>
#include <tuple>
#include <userver/formats/json/value_builder.hpp>

#include "agent/fields.hpp"
#include "agent/lua.hpp"

namespace factoryctl::agent {

namespace {

namespace json = userver::formats::json;
using namespace fields;

auto objectOrMissing(const json::Value& v, std::string_view path)
    -> Result<void> {
  if (!v.IsMissing() && !v.IsObject()) return fail(path, "not an object");
  return {};
}

auto onlyFields(const json::Value& v, std::string_view path,
                std::initializer_list<std::string_view> known) -> Result<void> {
  for (auto it = v.begin(); it != v.end(); ++it) {
    const auto name = it.GetName();
    bool ok = false;
    for (const auto k : known) ok = ok || k == name;
    if (!ok) return fail(std::format("{}.{}", path, name), "unknown field");
  }
  return {};
}

auto optionalNumber(const json::Value& v, std::string_view path, double min,
                    bool strict) -> Result<void> {
  if (v.IsMissing() || v.IsNull()) return {};
  if (!v.IsDouble()) return fail(path, "not a number");
  const auto n = v.As<double>();
  if (strict ? !(n > min) : !(n >= min)) {
    return fail(path, strict ? "must be > 0" : "must be >= 0");
  }
  return {};
}

}  // namespace

auto checkQftSettings(const json::Value& settings, const json::Value& catalog)
    -> Result<void> {
  if (!settings.IsObject()) return fail("settings", "not an object");
  if (auto ok = onlyFields(settings, "settings", {"mode", "recipes", "version"});
      !ok) {
    return ok;
  }
  const auto mode = text(settings["mode"], "settings.mode");
  if (!mode) return std::unexpected(mode.error());
  if (*mode != "auto" && *mode != "deficit" && *mode != "surplus") {
    return fail("settings.mode", "not auto, deficit or surplus");
  }
  // What the catalog has: recipe id -> its circuits.
  std::map<std::string, std::set<std::int64_t>> known;
  const bool checkCatalog = catalog.IsArray() && catalog.GetSize() > 0;
  if (checkCatalog) {
    for (const auto& r : catalog) {
      auto& circuits = known[r["id"].As<std::string>("")];
      for (const auto& o : r["outputs"]) {
        circuits.insert(o["circuit"].As<std::int64_t>(0));
      }
    }
  }
  const auto& recipes = settings["recipes"];
  if (recipes.IsMissing()) return {};
  if (!recipes.IsArray()) return fail("settings.recipes", "not an array");
  std::set<std::string> seen;
  for (std::size_t i = 0; i < recipes.GetSize(); ++i) {
    const auto path = std::format("settings.recipes[{}]", i);
    const auto& r = recipes[i];
    if (!r.IsObject()) return fail(path, "not an object");
    if (auto ok = onlyFields(r, path, {"id", "enabled", "trigger", "outputs"});
        !ok) {
      return ok;
    }
    if (const auto& e = r["enabled"]; !e.IsMissing() && !e.IsBool()) {
      return fail(std::format("{}.enabled", path), "not a boolean");
    }
    if (const auto& t = r["trigger"]; !t.IsMissing()) {
      if (!t.IsString() || (t.As<std::string>() != "deficit" && t.As<std::string>() != "surplus")) {
        return fail(std::format("{}.trigger", path), "not deficit or surplus");
      }
    }
    const auto id = text(r["id"], std::format("{}.id", path));
    if (!id) return std::unexpected(id.error());
    if (!seen.insert(*id).second) return fail(path, std::format("{} twice", *id));
    if (checkCatalog && !known.contains(*id)) {
      return fail(std::format("{}.id", path), std::format("no recipe {}", *id));
    }
    const auto& outputs = r["outputs"];
    if (outputs.IsMissing()) continue;
    if (!outputs.IsArray()) return fail(std::format("{}.outputs", path), "not an array");
    std::set<std::int64_t> circuits;
    for (std::size_t j = 0; j < outputs.GetSize(); ++j) {
      const auto opath = std::format("{}.outputs[{}]", path, j);
      const auto& o = outputs[j];
      if (!o.IsObject()) return fail(opath, "not an object");
      if (auto ok = onlyFields(o, opath, {"circuit", "target", "priority", "weight"});
          !ok) {
        return ok;
      }
      const auto circuit = integer(o["circuit"], std::format("{}.circuit", opath));
      if (!circuit) return std::unexpected(circuit.error());
      if (*circuit < 1) return fail(std::format("{}.circuit", opath), "must be >= 1");
      if (!circuits.insert(*circuit).second) {
        return fail(std::format("{}.circuit", opath), "twice");
      }
      if (checkCatalog && !known[*id].contains(*circuit)) {
        return fail(std::format("{}.circuit", opath),
                    std::format("{} has no output {}", *id, *circuit));
      }
      for (const auto& [field, min, strict] :
           {std::tuple{"target", 0.0, true}, std::tuple{"priority", 0.0, true},
            std::tuple{"weight", 0.0, false}}) {
        if (auto ok = optionalNumber(o[field], std::format("{}.{}", opath, field),
                                     min, strict);
            !ok) {
          return ok;
        }
      }
    }
  }
  return {};
}

auto mergeCatalog(const json::Value& settings, const json::Value& catalog)
    -> std::optional<json::Value> {
  if (!catalog.IsArray()) return std::nullopt;
  std::set<std::string> have;
  for (const auto& r : settings["recipes"]) have.insert(r["id"].As<std::string>(""));
  json::ValueBuilder recipes(settings["recipes"].IsArray()
                                 ? json::ValueBuilder{settings["recipes"]}
                                 : json::ValueBuilder{userver::formats::common::Type::kArray});
  bool added = false;
  for (const auto& r : catalog) {
    const auto id = r["id"].As<std::string>("");
    if (id.empty() || have.contains(id)) continue;
    json::ValueBuilder entry;
    entry["id"] = id;
    json::ValueBuilder outputs(userver::formats::common::Type::kArray);
    for (const auto& o : r["outputs"]) {
      json::ValueBuilder out;
      out["circuit"] = o["circuit"];
      for (const auto* field : {"target", "priority", "weight"}) {
        if (!o[field].IsMissing() && !o[field].IsNull()) out[field] = o[field];
      }
      outputs.PushBack(std::move(out));
    }
    entry["outputs"] = std::move(outputs);
    recipes.PushBack(std::move(entry));
    added = true;
  }
  if (!added) return std::nullopt;
  json::ValueBuilder out(settings);
  out["recipes"] = std::move(recipes);
  return out.ExtractValue();
}

auto parseQftPoll(const json::Value& body) -> Result<QftReport> {
  if (!body.IsObject()) return fail("qft poll", "not an object");
  QftReport r;
  const auto& setup = body["setup"];
  if (!setup.IsObject()) return fail("setup", "not an object");
  const auto name = text(setup["name"], "setup.name");
  if (!name) return std::unexpected(name.error());
  if (name->empty()) return fail("setup.name", "empty");
  r.name = *name;
  if (const auto& c = setup["coords"]; !c.IsMissing() && !c.IsNull()) {
    if (!c.IsObject()) return fail("setup.coords", "not an object");
    Coords coords;
    for (const auto& [field, out] :
         {std::pair{"x", &coords.x}, std::pair{"y", &coords.y}, std::pair{"z", &coords.z}}) {
      const auto n = integer(c[field], std::format("setup.coords.{}", field));
      if (!n) return std::unexpected(n.error());
      *out = *n;
    }
    r.coords = coords;
  }
  const auto run = text(body["run"], "run");
  if (!run) return std::unexpected(run.error());
  r.run = *run;
  const auto version = integer(body["settings"], "settings");
  if (!version) return std::unexpected(version.error());
  r.settings = *version;

  const auto& catalog = body["catalog"];
  if (!catalog.IsMissing()) {
    if (!catalog.IsArray()) return fail("catalog", "not an array");
    for (std::size_t i = 0; i < catalog.GetSize(); ++i) {
      const auto& e = catalog[i];
      if (!e.IsObject()) return fail(std::format("catalog[{}]", i), "not an object");
      const auto id = text(e["id"], std::format("catalog[{}].id", i));
      if (!id) return std::unexpected(id.error());
      if (!e["outputs"].IsArray()) {
        return fail(std::format("catalog[{}].outputs", i), "not an array");
      }
    }
    r.catalog = catalog;
  }
  if (!body["offer"].IsMissing()) {
    if (auto ok = checkQftSettings(body["offer"], catalog); !ok) {
      return std::unexpected(ok.error());
    }
    r.offer = body["offer"];
  }

  for (const auto* field : {"state", "plasma"}) {
    if (auto ok = objectOrMissing(body[field], field); !ok) {
      return std::unexpected(ok.error());
    }
  }
  if (!body["recipes"].IsMissing() && !body["recipes"].IsArray()) {
    return fail("recipes", "not an array");
  }
  json::ValueBuilder status;
  status["state"] = body["state"].IsMissing() ? json::ValueBuilder{} : json::ValueBuilder{body["state"]};
  status["recipes"] = body["recipes"].IsMissing()
                          ? json::ValueBuilder{userver::formats::common::Type::kArray}
                          : json::ValueBuilder{body["recipes"]};
  status["plasma"] = body["plasma"].IsMissing() ? json::ValueBuilder{} : json::ValueBuilder{body["plasma"]};
  r.status = status.ExtractValue();

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

auto replyQftPoll(const QftReply& reply) -> Result<std::string> {
  json::ValueBuilder b;
  b["poll"] = reply.poll.count();
  if (reply.settings) b["settings"] = *reply.settings;
  if (reply.offer) b["offer"] = true;
  return toLua(b.ExtractValue());
}

}  // namespace factoryctl::agent
