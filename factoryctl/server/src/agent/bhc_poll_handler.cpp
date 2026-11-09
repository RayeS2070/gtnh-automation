#include "agent/bhc_poll_handler.hpp"

#include <format>
#include <userver/components/component_context.hpp>
#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/transaction.hpp>
#include <userver/utils/datetime.hpp>

#include "agent/bhc_protocol.hpp"
#include "agent/scan_handler.hpp"
#include "db/bhc.hpp"
#include "db/events.hpp"
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

auto levelOf(const std::string& agentLevel) -> std::string {
  return agentLevel == "err"   ? "error"
         : agentLevel == "wrn" ? "warning"
         : agentLevel == "dbg" ? "debug"
                               : "info";
}

}  // namespace

BhcPollHandler::BhcPollHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()) {
  context.FindComponent<db::Schema>();
}

auto BhcPollHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("text/plain; charset=utf-8");
  json::Value body;
  try {
    body = json::FromString(request.RequestBody());
  } catch (const json::Exception& e) {
    LOG_WARNING() << "bhc poll: bad JSON: " << e.what();
    return replyLua(std::string("bad JSON: ") + e.what());
  }
  const auto report = parseBhcPoll(body);
  if (!report) {
    LOG_WARNING() << "bhc poll: " << report.error();
    return replyLua(report.error());
  }

  const auto now = userver::utils::datetime::Now();
  const auto& name = report->name;
  auto tx = db_->Begin(pg::ClusterHostType::kMaster, {});
  auto setup = db::lockBhcSetup(tx, name);
  std::vector<db::Event> events;
  auto told = [&](std::string text, std::string level = "info") {
    events.push_back({.at = now,
                      .level = std::move(level),
                      .source = "bhc",
                      .text = std::format("[{}] {}", name, text)});
  };

  // Two BHCs given one name would mix their settings and sessions.
  if (setup && setup->coords && report->coords &&
      *setup->coords != *report->coords) {
    const auto& c = *setup->coords;
    const auto error = std::format(
        "the name {} belongs to the BHC at {},{},{}: give this one its own "
        "name "
        "(name in config.lua), or forget the other in the web",
        name, c.x, c.y, c.z);
    LOG_WARNING() << "bhc poll: " << error;
    return replyLua(error);
  }
  if (!setup) {
    setup = db::BhcSetup{.name = name};
    told(report->coords
             ? std::format("new setup at {},{},{}", report->coords->x,
                           report->coords->y, report->coords->z)
             : std::string("new setup"));
  }
  if (!setup->coords) setup->coords = report->coords;
  setup->run = report->run;
  setup->status = report->status;
  setup->seenAt = now;

  if (setup->settingsVersion == 0 && report->offer) {
    // The first meeting: the agent's own become version 1.
    setup->settings = withoutVersion(*report->offer);
    setup->settingsVersion = 1;
    told("settings taken from the agent: " +
         db::describeBhcSettings(*setup->settings));
  }
  for (const auto& l : report->log) {
    events.push_back({.at = now,
                      .level = levelOf(l.level),
                      .source = "bhc",
                      .text = std::format("[{}] {}", name, l.text),
                      .agentUptime = l.t});
  }
  db::saveBhcSetup(tx, *setup, now);
  const auto added =
      db::addBhcSessions(tx, name, report->run, report->sessions, now);
  db::addEvents(tx, events);
  tx.Commit();

  for (const auto& l : report->log) {
    LOG_INFO() << userver::logging::LogExtra{{"source", "bhc"},
                                             {"setup", name},
                                             {"agent_uptime", l.t}}
               << l.text;
  }
  if (!added.empty()) {
    LOG_INFO() << "bhc " << name << ": " << added.size() << " sessions";
  }

  BhcReply reply;
  if (setup->settingsVersion == 0) {
    reply.offer = true;
  } else if (setup->settingsVersion != report->settings && setup->settings) {
    json::ValueBuilder s;
    s["version"] = setup->settingsVersion;
    for (auto it = setup->settings->begin(); it != setup->settings->end();
         ++it) {
      s[it.GetName()] = *it;
    }
    reply.settings = s.ExtractValue();
  }
  const auto lua = replyBhcPoll(reply);
  if (!lua) {
    LOG_ERROR() << "bhc poll: reply: " << lua.error();
    return replyLua(lua.error());
  }
  return *lua;
}

}  // namespace factoryctl::agent
