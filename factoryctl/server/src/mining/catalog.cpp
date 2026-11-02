#include "mining/catalog.hpp"

#include <format>
#include <stdexcept>
// Registers the embedded mining_catalog.json: in exactly one source file.
#include <generated/mining_catalog.json.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/parse/common_containers.hpp>
#include <userver/utils/resources.hpp>

#include "core/mechanics.hpp"

namespace factoryctl::mining {

namespace {

namespace json = userver::formats::json;

// The data is ours (generated): a field missing or of another type is a bug
// there, reported with its path.
template <typename T>
auto field(const json::Value& v, std::string_view name) -> T {
  const auto& f = v[std::string(name)];
  if (f.IsMissing()) {
    throw std::runtime_error(std::format("{}.{}: missing", v.GetPath(), name));
  }
  return f.As<T>();
}

auto itemOf(const json::Value& v) -> domain::MiningItem {
  return {.key = {.name = field<std::string>(v, "name"),
                  .damage = field<std::int32_t>(v, "damage")},
          .label = field<std::string>(v, "label")};
}

auto droneTier(const std::string& name) -> int {
  for (int i = 0; i < core::kDroneTierCount; ++i) {
    if (core::kDroneTierNames[static_cast<std::size_t>(i)] == name) return i;
  }
  throw std::runtime_error("unknown drone tier " + name);
}

}  // namespace

auto parseCatalog(const json::Value& json)
    -> std::expected<domain::MiningCatalog, std::string> {
  try {
    domain::MiningCatalog c;
    c.smc.gt5uVersion = field<std::string>(json["source"], "gt5u");
    for (const auto& a : json["asteroids"]) {
      core::Asteroid ast{
          .name = field<std::string>(a, "name"),
          .minSize = field<int>(a, "minSize"),
          .maxSize = field<int>(a, "maxSize"),
          .minDistance = field<int>(a, "minDistance"),
          .maxDistance = field<int>(a, "maxDistance"),
          .computation = field<int>(a, "computation"),
          .minModuleTier = field<int>(a, "minModuleTier"),
          .duration = field<int>(a, "duration"),
          .eut = field<std::int64_t>(a, "eut"),
          .scaleEUt = field<bool>(a, "scaleEUt"),
          .firstDroneTier = droneTier(a["droneTiers"][0].As<std::string>()),
          .lastDroneTier = droneTier(a["droneTiers"][1].As<std::string>()),
          .weight = field<int>(a, "weight"),
      };
      std::vector<domain::MiningOutput> outputs;
      for (const auto& o : a["outputs"]) {
        ast.outputs.push_back(
            {.name = field<std::string>(o, "name"),
             .prefix = o["prefix"].As<std::optional<std::string>>(),
             .amount = field<int>(o, "amount"),
             .chance = field<int>(o, "chance")});
        domain::MiningOutput out{.item = itemOf(o["item"])};
        for (const auto& p : o["products"]) out.products.push_back(itemOf(p));
        outputs.push_back(std::move(out));
      }
      c.smc.asteroids.push_back(std::move(ast));
      c.outputs.push_back(std::move(outputs));
    }
    c.recipes = core::expand(c.smc.asteroids);
    for (const auto& d : json["drones"]) {
      c.drones.push_back({.tier = field<int>(d, "tier"),
                          .name = field<std::string>(d, "name"),
                          .drone = itemOf(d["drone"]),
                          .tip = itemOf(d["tip"]),
                          .rod = itemOf(d["rod"])});
      if (c.drones.back().tier + 1 != static_cast<int>(c.drones.size())) {
        throw std::runtime_error("drones: not by tier");
      }
    }
    for (const auto& p : json["plasmas"]) {
      c.plasmas.push_back({.tier = field<int>(p, "tier"),
                           .fluid = field<std::string>(p, "name"),
                           .label = field<std::string>(p, "label"),
                           .usage = field<int>(p, "usage")});
    }
    return c;
  } catch (const std::exception& e) {
    return std::unexpected(std::string(e.what()));
  }
}

auto catalog() -> const domain::MiningCatalog& {
  static const domain::MiningCatalog c = [] {
    auto parsed = parseCatalog(
        json::FromString(userver::utils::FindResource("mining_catalog")));
    if (!parsed) throw std::runtime_error("mining catalog: " + parsed.error());
    return std::move(*parsed);
  }();
  return c;
}

}  // namespace factoryctl::mining
