#include "api/mining_handler.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <map>
#include <set>
#include <userver/components/component_context.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/http/parser/http_request_parse_args.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_status.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/transaction.hpp>
#include <userver/utils/datetime.hpp>

#include "api/json.hpp"
#include "api/user.hpp"
#include "config/dynamic.hpp"
#include "db/mining.hpp"
#include "db/schema.hpp"
#include "mining/catalog.hpp"

namespace factoryctl::api {

namespace {

namespace json = userver::formats::json;
namespace pg = userver::storages::postgres;
using userver::server::http::HttpStatus;

auto cluster(const userver::components::ComponentContext& context) {
  context.FindComponent<db::Schema>();
  return context.FindComponent<userver::components::Postgres>("postgres-db")
      .GetCluster();
}

auto fail(const userver::server::http::HttpRequest& request, HttpStatus status,
          std::string_view message) -> std::string {
  request.GetHttpResponse().SetStatus(status);
  return errorJson(message);
}

auto utc(domain::TimePoint t) -> std::string {
  return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(t));
}

auto itemJson(const domain::ItemKey& k) -> json::ValueBuilder {
  json::ValueBuilder b;
  b["name"] = k.name;
  b["damage"] = k.damage;
  b["label"] = mining::labelOf(k);
  return b;
}

// {"threshold": 1000000, "priority": 1, "enabled": true,
//  "ores": [{"name", "damage"}]}; priority and enabled may be left out.
auto parseTarget(const json::Value& body)
    -> std::expected<db::MiningTargetSettings, std::string> {
  if (!body.IsObject()) return std::unexpected("not an object");
  for (auto it = body.begin(); it != body.end(); ++it) {
    const auto n = it.GetName();
    if (n != "threshold" && n != "priority" && n != "enabled" && n != "ores") {
      return std::unexpected(n + ": unknown field");
    }
  }
  db::MiningTargetSettings s;
  if (!body["threshold"].IsInt64()) return std::unexpected("threshold: a whole number");
  s.threshold = body["threshold"].As<std::int64_t>();
  if (!body["priority"].IsMissing()) {
    if (!body["priority"].IsDouble()) return std::unexpected("priority: a number");
    s.priority = body["priority"].As<double>();
  }
  if (!body["enabled"].IsMissing()) {
    if (!body["enabled"].IsBool()) return std::unexpected("enabled: true or false");
    s.enabled = body["enabled"].As<bool>();
  }
  const auto& ores = body["ores"];
  if (!ores.IsArray()) return std::unexpected("ores: an array");
  std::set<domain::ItemKey> seen;
  for (std::size_t i = 0; i < ores.GetSize(); ++i) {
    const auto& o = ores[i];
    if (!o.IsObject() || !o["name"].IsString() || !o["damage"].IsInt()) {
      return std::unexpected(std::format("ores[{}]: {{name, damage}}", i));
    }
    domain::ItemKey k{.name = o["name"].As<std::string>(),
                      .damage = o["damage"].As<std::int32_t>()};
    if (seen.insert(k).second) s.ores.push_back(std::move(k));
  }
  return s;
}

}  // namespace

MiningHandler::MiningHandler(const userver::components::ComponentConfig& config,
                             const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(cluster(context)),
      config_(context.FindComponent<userver::components::DynamicConfig>()
                  .GetSource()) {}

auto MiningHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  const auto& catalog = mining::catalog();
  const auto settings = config_.GetCopy(config::kMining);
  json::ValueBuilder out;
  json::ValueBuilder setups(json::Type::kArray);
  for (const auto& s : db::listMiningSetups(*db_)) {
    json::ValueBuilder e;
    e["name"] = s.name;
    e["run"] = s.run;
    e["seenAt"] = utc(s.seenAt);
    e["status"] = s.status;
    e["plan"] = s.plan;
    setups.PushBack(std::move(e));
  }
  out["setups"] = std::move(setups);

  auto tx = db_->Begin(pg::ClusterHostType::kMaster, {});
  json::ValueBuilder targets(json::Type::kArray);
  for (const auto& t : db::listMiningTargets(tx)) {
    auto e = toJson(t.goods);
    e["threshold"] = static_cast<std::int64_t>(t.target.threshold);
    e["priority"] = t.target.priority;
    e["enabled"] = t.target.enabled;
    json::ValueBuilder ores(json::Type::kArray);
    for (const auto& o : t.target.ores) ores.PushBack(itemJson(o));
    e["ores"] = std::move(ores);
    targets.PushBack(std::move(e));
  }
  out["targets"] = std::move(targets);
  std::vector<std::string> names;
  for (const auto& p : catalog.plasmas) names.push_back(p.fluid);
  const int plasma = domain::bestPlasma(catalog, db::fluidStock(tx, names),
                                        settings.plasmaMinimum);
  tx.Commit();
  if (plasma > 0) {
    out["plasma"]["tier"] = plasma;
    out["plasma"]["label"] = catalog.plasmas[static_cast<std::size_t>(plasma - 1)].label;
  } else {
    out["plasma"] = json::ValueBuilder(json::Type::kNull);
  }
  out["bufferLimit"] = settings.bufferLimit;
  return json::ToString(out.ExtractValue());
}

MiningTargetHandler::MiningTargetHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto MiningTargetHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  const auto id = parseId(request.GetPathArg("goods_id"));
  if (!id) return fail(request, HttpStatus::kBadRequest, "bad goods id");
  const auto now = userver::utils::datetime::Now();
  if (request.GetMethod() == userver::server::http::HttpMethod::kDelete) {
    if (!db::deleteMiningTarget(*db_, *id, now, userOf(request))) {
      return fail(request, HttpStatus::kNotFound, "no mining target");
    }
    return R"({"ok":true})";
  }
  json::Value body;
  try {
    body = json::FromString(request.RequestBody());
  } catch (const json::Exception& e) {
    return fail(request, HttpStatus::kBadRequest, std::string("bad JSON: ") + e.what());
  }
  const auto s = parseTarget(body);
  if (!s) return fail(request, HttpStatus::kBadRequest, s.error());
  if (auto ok = db::setMiningTarget(*db_, *id, *s, now, userOf(request)); !ok) {
    return fail(request, HttpStatus::kBadRequest, ok.error());
  }
  LOG_INFO() << "mining target of goods " << *id << ": keep " << s->threshold;
  return R"({"ok":true})";
}

MiningTargetsHandler::MiningTargetsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto MiningTargetsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  json::Value body;
  try {
    body = json::FromString(request.RequestBody());
  } catch (const json::Exception& e) {
    return fail(request, HttpStatus::kBadRequest, std::string("bad JSON: ") + e.what());
  }
  if (!body.IsObject()) return fail(request, HttpStatus::kBadRequest, "not an object");
  const auto& p = body["product"];
  if (!p.IsObject() || !p["name"].IsString() || !p["damage"].IsInt()) {
    return fail(request, HttpStatus::kBadRequest,
                "product: {name, damage, variant, label}");
  }
  const domain::ItemKey product{.name = p["name"].As<std::string>(),
                                .damage = p["damage"].As<std::int32_t>(),
                                .variant = p["variant"].As<std::string>("")};
  const auto label = p["label"].As<std::string>("");
  json::ValueBuilder rest(body);
  rest.Remove("product");
  const auto s = parseTarget(rest.ExtractValue());
  if (!s) return fail(request, HttpStatus::kBadRequest, s.error());
  const auto id = db::addMiningTarget(*db_, product, label, *s,
                                      userver::utils::datetime::Now(), userOf(request));
  if (!id) return fail(request, HttpStatus::kBadRequest, id.error());
  LOG_INFO() << "mining target " << label << " (goods " << *id << "): keep " << s->threshold;
  json::ValueBuilder out;
  out["goodsId"] = *id;
  return json::ToString(out.ExtractValue());
}

auto MiningOresHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  // The same for every request: made once.
  static const std::string answer = [] {
    const auto& c = mining::catalog();
    struct Ore {
      std::set<std::string> asteroids;
      std::set<domain::ItemKey> products;
    };
    std::map<domain::ItemKey, Ore> ores;
    for (std::size_t a = 0; a < c.outputs.size(); ++a) {
      for (const auto& o : c.outputs[a]) {
        auto& ore = ores[o.item.key];
        ore.asteroids.insert(c.smc.asteroids[a].name);
        for (const auto& p : o.products) ore.products.insert(p.key);
      }
    }
    std::vector<std::pair<std::string, const domain::ItemKey*>> byLabel;
    for (const auto& [key, ore] : ores) byLabel.emplace_back(mining::labelOf(key), &key);
    std::ranges::sort(byLabel);
    json::ValueBuilder out(json::Type::kArray);
    for (const auto& [label, key] : byLabel) {
      const auto& ore = ores.at(*key);
      auto e = itemJson(*key);
      json::ValueBuilder asteroids(json::Type::kArray);
      for (const auto& n : ore.asteroids) asteroids.PushBack(n);
      e["asteroids"] = std::move(asteroids);
      json::ValueBuilder products(json::Type::kArray);
      for (const auto& p : ore.products) {
        json::ValueBuilder pe;
        pe["name"] = p.name;
        pe["damage"] = p.damage;
        products.PushBack(std::move(pe));
      }
      e["products"] = std::move(products);
      out.PushBack(std::move(e));
    }
    return json::ToString(out.ExtractValue());
  }();
  return answer;
}

MiningSetupHandler::MiningSetupHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto MiningSetupHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  const auto name = userver::http::parser::UrlDecode(request.GetPathArg("name"));
  if (!db::deleteMiningSetup(*db_, name, userver::utils::datetime::Now(),
                             userOf(request))) {
    return fail(request, HttpStatus::kNotFound, "no such setup");
  }
  return R"({"ok":true})";
}

}  // namespace factoryctl::api
