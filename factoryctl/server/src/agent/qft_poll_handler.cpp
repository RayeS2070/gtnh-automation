#include "agent/qft_poll_handler.hpp"

#include <format>
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

#include "agent/qft_protocol.hpp"
#include "agent/scan_handler.hpp"
#include "config/dynamic.hpp"
#include "db/events.hpp"
#include "db/qft.hpp"
#include "db/schema.hpp"

namespace factoryctl::agent {

namespace {

namespace json = userver::formats::json;
namespace pg = userver::storages::postgres;

auto withoutVersion(const json::Value& v) -> json::Value {
  json::ValueBuilder b(v);
  b.Remove("version");
  return b.ExtractValue();
}

auto describe(const json::Value& state) -> std::string {
  const auto recipe = state["recipe"].As<std::string>("");
  if (recipe.empty()) return "stands still";
  auto text = "runs " + recipe;
  if (state["fermium"].As<bool>(false)) text += " + fermium";
  if (!state["neptunium"].IsMissing() && !state["neptunium"].IsNull()) {
    text += std::format(" + neptunium@{}", state["neptunium"].As<int>(0));
  }
  return text;
}

}  // namespace

QftPollHandler::QftPollHandler(const userver::components::ComponentConfig& config,
                               const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()),
      config_(context.FindComponent<userver::components::DynamicConfig>()
                  .GetSource()) {
  context.FindComponent<db::Schema>();
}

auto QftPollHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("text/plain; charset=utf-8");
  json::Value body;
  try {
    body = json::FromString(request.RequestBody());
  } catch (const json::Exception& e) {
    LOG_WARNING() << "qft poll: bad JSON: " << e.what();
    return replyLua(std::string("bad JSON: ") + e.what());
  }
  const auto report = parseQftPoll(body);
  if (!report) {
    LOG_WARNING() << "qft poll: " << report.error();
    return replyLua(report.error());
  }

  const auto now = userver::utils::datetime::Now();
  const auto& name = report->name;
  auto tx = db_->Begin(pg::ClusterHostType::kMaster, {});
  auto setup = db::lockQftSetup(tx, name);
  std::vector<db::Event> events;
  auto told = [&](std::string text, std::string level = "info") {
    events.push_back({.at = now,
                      .level = std::move(level),
                      .source = "qft",
                      .text = std::format("[{}] {}", name, text)});
  };

  // Two QFTs given one name would mix their settings and reports: the second
  // one is refused until it gets its own name (or the first is forgotten).
  if (setup && setup->coords && report->coords && *setup->coords != *report->coords) {
    const auto& c = *setup->coords;
    const auto error = std::format(
        "the name {} belongs to the QFT at {},{},{}: give this one its own name "
        "(components.name), or forget the other in the web",
        name, c.x, c.y, c.z);
    LOG_WARNING() << "qft poll: " << error;
    return replyLua(error);
  }
  if (!setup) {
    setup = db::QftSetup{.name = name, .catalog = json::FromString("[]")};
    told(report->coords ? std::format("new setup at {},{},{}", report->coords->x,
                                      report->coords->y, report->coords->z)
                        : std::string("new setup"));
  }
  const auto before = setup->status["state"];
  if (!setup->coords) setup->coords = report->coords;
  setup->run = report->run;
  setup->status = report->status;
  setup->seenAt = now;
  if (report->catalog) setup->catalog = *report->catalog;
  if (const auto after = setup->status["state"];
      describe(before) != describe(after) && !after.IsMissing() && !after.IsNull()) {
    told(describe(after));
  }

  if (setup->settingsVersion == 0 && report->offer) {
    // The first meeting: the agent's own become version 1.
    setup->settings = withoutVersion(*report->offer);
    setup->settingsVersion = 1;
    told("settings taken from the agent");
  } else if (setup->settingsVersion > 0 && setup->settings && report->catalog) {
    if (auto merged = mergeCatalog(*setup->settings, *report->catalog)) {
      setup->settings = *merged;
      setup->settingsVersion += 1;
      told("settings: recipes new in its config added");
    }
  }
  for (const auto& l : report->log) {
    events.push_back({.at = now,
                      .level = l.level == "err"   ? "error"
                               : l.level == "wrn" ? "warning"
                               : l.level == "dbg" ? "debug"
                                                  : "info",
                      .source = "qft",
                      .text = std::format("[{}] {}", name, l.text),
                      .agentUptime = l.t});
  }
  db::saveQftSetup(tx, *setup, now);
  db::addEvents(tx, events);
  tx.Commit();

  for (const auto& l : report->log) {
    LOG_INFO() << userver::logging::LogExtra{{"source", "qft"}, {"setup", name},
                                             {"agent_uptime", l.t}}
               << l.text;
  }

  QftReply reply{.poll = config_.GetCopy(config::kPoll).poll};
  if (setup->settingsVersion == 0) {
    reply.offer = true;
  } else if (setup->settingsVersion != report->settings && setup->settings) {
    json::ValueBuilder s;
    s["version"] = setup->settingsVersion;
    for (auto it = setup->settings->begin(); it != setup->settings->end(); ++it) {
      s[it.GetName()] = *it;
    }
    reply.settings = s.ExtractValue();
  }
  const auto lua = replyQftPoll(reply);
  if (!lua) {
    LOG_ERROR() << "qft poll: reply: " << lua.error();
    return replyLua(lua.error());
  }
  return *lua;
}

}  // namespace factoryctl::agent
