#include "agent/mining_poll_handler.hpp"

#include <chrono>
#include <format>
#include <map>
#include <set>
#include <userver/components/component_context.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/transaction.hpp>
#include <userver/utils/datetime.hpp>

#include "agent/mining_protocol.hpp"
#include "agent/scan_handler.hpp"
#include "config/dynamic.hpp"
#include "db/events.hpp"
#include "db/mining.hpp"
#include "db/schema.hpp"
#include "mining/catalog.hpp"

namespace factoryctl::agent {

namespace {

namespace json = userver::formats::json;
namespace pg = userver::storages::postgres;

// Another setup's plan counts while it polls; one that went quiet does not.
constexpr std::chrono::minutes kFresh{5};
constexpr int kDefaultSlots = 63;

auto settingJson(const domain::MiningSetting& s) -> json::ValueBuilder {
  json::ValueBuilder b;
  b["droneTier"] = s.droneTier;
  b["distance"] = s.distance;
  b["plasmaTier"] = s.plasmaTier;
  b["parallel"] = s.parallel;
  return b;
}

auto settingOf(const json::Value& v) -> std::optional<domain::MiningSetting> {
  if (!v.IsObject()) return std::nullopt;
  return domain::MiningSetting{.droneTier = v["droneTier"].As<int>(0),
                               .distance = v["distance"].As<int>(1),
                               .plasmaTier = v["plasmaTier"].As<int>(1),
                               .parallel = v["parallel"].As<int>(1)};
}

// The plan as kept in mining_setups.plan, per module id.
struct Planned {
  int tier = 1;
  std::optional<domain::MiningSetting> setting;
};

auto plannedOf(const json::Value& plan) -> std::map<std::string, Planned> {
  std::map<std::string, Planned> out;
  for (const auto& e : plan) {
    out[e["module"].As<std::string>("")] = {.tier = e["tier"].As<int>(1),
                                            .setting = settingOf(e["setting"])};
  }
  return out;
}

auto describe(const std::optional<domain::MiningSetting>& s) -> std::string {
  if (!s) return "idle";
  const auto& c = mining::catalog();
  const auto drone =
      s->droneTier < static_cast<int>(c.drones.size())
          ? c.drones[static_cast<std::size_t>(s->droneTier)].name
          : std::to_string(s->droneTier);
  const auto plasma =
      s->plasmaTier >= 1 && s->plasmaTier <= static_cast<int>(c.plasmas.size())
          ? c.plasmas[static_cast<std::size_t>(s->plasmaTier - 1)].label
          : std::to_string(s->plasmaTier);
  return std::format("{} drone, distance {}, {}, {} parallels", drone,
                     s->distance, plasma, s->parallel);
}

auto levelOf(const std::string& l) -> std::string {
  return l == "err" ? "error" : l == "wrn" ? "warning" : l == "dbg" ? "debug" : "info";
}

}  // namespace

MiningPollHandler::MiningPollHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()),
      config_(context.FindComponent<userver::components::DynamicConfig>()
                  .GetSource()) {
  context.FindComponent<db::Schema>();
  (void)mining::catalog();  // a bad catalog stops the server at start
}

auto MiningPollHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("text/plain; charset=utf-8");
  json::Value body;
  try {
    body = json::FromString(request.RequestBody());
  } catch (const json::Exception& e) {
    LOG_WARNING() << "mining poll: bad JSON: " << e.what();
    return replyLua(std::string("bad JSON: ") + e.what());
  }
  const auto report = parseMiningPoll(body);
  if (!report) {
    LOG_WARNING() << "mining poll: " << report.error();
    return replyLua(report.error());
  }

  const auto& catalog = mining::catalog();
  const auto settings = config_.GetCopy(config::kMining);
  const auto now = userver::utils::datetime::Now();
  const auto& name = report->name;
  auto tx = db_->Begin(pg::ClusterHostType::kMaster, {});
  std::vector<db::Event> events;
  auto told = [&](std::string text, std::string level = "info") {
    events.push_back({.at = now,
                      .level = std::move(level),
                      .source = "mining",
                      .text = std::format("[{}] {}", name, text)});
  };

  auto setup = db::lockMiningSetup(tx, name);
  if (!setup) {
    setup = db::MiningSetup{.name = name, .plan = json::FromString("[]")};
    told(std::format("new setup, {} module{}", report->modules.size(),
                     report->modules.size() == 1 ? "" : "s"));
  }
  const auto before = plannedOf(setup->plan);
  // A module's error as last reported: told again only when it changes.
  std::map<std::string, std::string> errorsBefore;
  // A new setup has no report yet (status missing): nothing to compare.
  const auto& reported = setup->status["modules"];
  for (const auto& m : reported.IsArray() ? reported : json::FromString("[]")) {
    errorsBefore[std::format("{},{},{}", m["x"].As<std::int64_t>(0), m["y"].As<std::int64_t>(0),
                             m["z"].As<std::int64_t>(0))] = m["error"].As<std::string>("");
  }

  // What the targets want, from the stock and this setup's buffer.
  const auto rows = db::listMiningTargets(tx);
  std::vector<domain::MiningTarget> targets;
  std::map<domain::ItemKey, double> stock;
  for (const auto& r : rows) {
    targets.push_back(r.target);
    stock[r.goods.key] = static_cast<double>(r.goods.amount);
  }
  const auto demand =
      domain::miningDemand(targets, stock, report->buffer, settings.bufferLimit);

  std::vector<std::string> plasmaNames;
  for (const auto& p : catalog.plasmas) plasmaNames.push_back(p.fluid);
  const int plasma = domain::bestPlasma(catalog, db::fluidStock(tx, plasmaNames),
                                        settings.plasmaMinimum);

  std::vector<std::pair<int, domain::MiningSetting>> others;
  for (const auto& other : db::listMiningSetups(tx, name)) {
    if (now - other.seenAt > kFresh) continue;
    for (const auto& [id, p] : plannedOf(other.plan)) {
      if (p.setting) others.emplace_back(p.tier, *p.setting);
    }
  }

  std::map<int, int> drones = report->pool;
  std::vector<domain::MiningModule> modules;
  for (const auto& m : report->modules) {
    if (m.drone) drones[*m.drone] += 1;
    const auto id = moduleId(m.at);
    const auto was = before.find(id);
    modules.push_back({.id = id,
                       .tier = m.tier,
                       .current = was != before.end() ? was->second.setting
                                                      : std::nullopt});
    if (!m.error.empty() && errorsBefore[id] != m.error) {
      told(std::format("{}: {}", id, m.error), "warning");
    }
  }
  // The drill parts of the drones there are, and the plasma set: kept stocked
  // by targets made once. A drone whose parts are neither there nor craftable
  // is left out: its module would not start.
  const auto parts = domain::miningDrillParts(catalog, drones);
  std::vector<db::MiningSupply> supplies;
  std::vector<domain::ItemKey> partKeys;
  for (const auto& p : parts) {
    supplies.push_back({p, settings.drillThreshold, settings.drillBatch});
    partKeys.push_back(p.key);
  }
  if (!settings.plasma.empty()) {
    if (const auto p = domain::miningPlasmaItem(catalog, settings.plasma)) {
      supplies.push_back({*p, settings.plasmaThreshold, settings.plasmaBatch});
    } else {
      LOG_LIMITED_WARNING() << "FACTORYCTL_MINING.plasma: " << settings.plasma
                            << " is not a mining plasma";
    }
  }
  for (auto& e : db::ensureMiningSupplies(tx, supplies, now)) events.push_back(std::move(e));
  const auto usable =
      domain::miningUsableDrones(catalog, drones, db::miningStock(tx, partKeys));
  std::set<std::string> missingBefore;
  const auto& was = setup->status["missing"];
  for (const auto& m : was.IsArray() ? was : json::FromString("[]")) {
    missingBefore.insert(m["label"].As<std::string>(""));
  }
  json::ValueBuilder missing(json::Type::kArray);
  std::set<std::string> missingNow;
  for (const auto& p : usable.missing) {
    missingNow.insert(p.label);
    if (!missingBefore.contains(p.label)) {
      told(std::format("no {} and no pattern for it: its drones are left out", p.label),
           "warning");
    }
    json::ValueBuilder e;
    e["name"] = p.key.name;
    e["damage"] = p.key.damage;
    e["label"] = p.label;
    missing.PushBack(std::move(e));
  }
  for (const auto& l : missingBefore) {
    if (!missingNow.contains(l)) told(std::format("{} is there: its drones are back", l));
  }

  const auto plan = domain::planMining(catalog, {.modules = modules,
                                                 .others = others,
                                                 .drones = usable.drones,
                                                 .plasmaTier = plasma,
                                                 .needs = demand});

  MiningReply reply{.poll = config_.GetCopy(config::kPoll).poll,
                    .keep = domain::miningWhitelist(
                        targets, stock, report->slots.value_or(kDefaultSlots)),
                    .bufferLimit = settings.bufferLimit};
  json::ValueBuilder kept(json::Type::kArray);
  for (const auto& a : plan) {
    const auto& m = *std::ranges::find_if(
        report->modules, [&](const auto& r) { return moduleId(r.at) == a.module; });
    reply.modules.push_back({.at = m.at, .setting = a.setting});
    const auto was = before.find(a.module);
    const auto wasSetting =
        was != before.end() ? was->second.setting : std::nullopt;
    if (was == before.end() || wasSetting != a.setting) {
      told(std::format("{}: {}", a.module, describe(a.setting)));
    }
    json::ValueBuilder e;
    e["module"] = a.module;
    e["tier"] = m.tier;
    if (a.setting) {
      e["setting"] = settingJson(*a.setting);
      e["value"] = a.value;
    }
    json::ValueBuilder rates(json::Type::kArray);
    for (const auto& [item, rate] : a.rates) {
      json::ValueBuilder r;
      r["name"] = item.name;
      r["damage"] = item.damage;
      r["label"] = mining::labelOf(item);
      r["rate"] = rate;
      rates.PushBack(std::move(r));
    }
    e["rates"] = std::move(rates);
    kept.PushBack(std::move(e));
  }
  if (plasma == 0 && !report->modules.empty() && !demand.empty() &&
      before.empty()) {
    told("no plasma in stock: every module stays idle", "warning");
  }

  setup->run = report->run;
  json::ValueBuilder status(report->status);
  status["missing"] = std::move(missing);
  setup->status = status.ExtractValue();
  setup->plan = kept.ExtractValue();
  setup->seenAt = now;
  for (const auto& l : report->log) {
    events.push_back({.at = now,
                      .level = levelOf(l.level),
                      .source = "mining",
                      .text = std::format("[{}] {}", name, l.text),
                      .agentUptime = l.t});
  }
  db::saveMiningSetup(tx, *setup);
  db::addEvents(tx, events);
  tx.Commit();

  for (const auto& l : report->log) {
    LOG_INFO() << userver::logging::LogExtra{{"source", "mining"}, {"setup", name},
                                             {"agent_uptime", l.t}}
               << l.text;
  }
  const auto lua = replyMiningPoll(reply, catalog);
  if (!lua) {
    LOG_ERROR() << "mining poll: reply: " << lua.error();
    return replyLua(lua.error());
  }
  return *lua;
}

}  // namespace factoryctl::agent
