#include "agent/pumps_poll_handler.hpp"

#include <format>
#include <userver/components/component_context.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/transaction.hpp>
#include <userver/utils/datetime.hpp>

#include "agent/pumps_protocol.hpp"
#include "agent/scan_handler.hpp"
#include "config/dynamic.hpp"
#include "db/events.hpp"
#include "db/pumps.hpp"
#include "db/schema.hpp"

namespace factoryctl::agent {

namespace {

namespace json = userver::formats::json;
namespace pg = userver::storages::postgres;

}  // namespace

PumpsPollHandler::PumpsPollHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()),
      config_(context.FindComponent<userver::components::DynamicConfig>()
                  .GetSource()) {
  context.FindComponent<db::Schema>();
}

auto PumpsPollHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("text/plain; charset=utf-8");
  json::Value body;
  try {
    body = json::FromString(request.RequestBody());
  } catch (const json::Exception& e) {
    LOG_WARNING() << "pumps poll: bad JSON: " << e.what();
    return replyLua(std::string("bad JSON: ") + e.what());
  }
  const auto report = parsePumpsPoll(body);
  if (!report) {
    LOG_WARNING() << "pumps poll: " << report.error();
    return replyLua(report.error());
  }

  const auto now = userver::utils::datetime::Now();
  auto tx = db_->Begin(pg::ClusterHostType::kMaster, {});
  auto settings = db::loadPumpSettings(tx);
  std::vector<db::Event> events;
  if (!settings && report->offer) {
    // The first meeting: the agent's own (config.lua's) become version 1, so
    // its targets need no typing in.
    settings = *report->offer;
    settings->version = 1;
    db::storePumpSettings(tx, *settings, now);
    events.push_back(
        {.at = now,
         .source = "pumps",
         .text = std::format("pumping settings taken from the agent: {} "
                             "targets, batch {} s",
                             settings->targets.size(), settings->batch)});
  }
  auto told = db::storePumpsReport(tx, *report, now);
  events.insert(events.end(), told.begin(), told.end());
  for (const auto& l : report->log) {
    events.push_back({.at = now,
                      .level = l.level == "err"   ? "error"
                               : l.level == "wrn" ? "warning"
                               : l.level == "dbg" ? "debug"
                                                  : "info",
                      .source = "pumps",
                      .text = l.text,
                      .agentUptime = l.t});
  }
  db::addEvents(tx, events);
  tx.Commit();

  for (const auto& l : report->log) {
    using userver::logging::Level;
    const auto level = l.level == "err"   ? Level::kError
                       : l.level == "wrn" ? Level::kWarning
                       : l.level == "dbg" ? Level::kDebug
                                          : Level::kInfo;
    LOG(level) << userver::logging::LogExtra{{"source", "pumps"},
                                             {"agent_uptime", l.t}}
               << l.text;
  }

  PumpsReply reply{.poll = config_.GetCopy(config::kPoll).poll};
  if (!settings) {
    reply.offer = true;
  } else if (settings->version != report->settings) {
    reply.settings = settings;
  }
  const auto lua = replyPumpsPoll(reply);
  if (!lua) {
    LOG_ERROR() << "pumps poll: reply: " << lua.error();
    return replyLua(lua.error());
  }
  return *lua;
}

}  // namespace factoryctl::agent
