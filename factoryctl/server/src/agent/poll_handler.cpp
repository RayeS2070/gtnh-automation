#include "agent/poll_handler.hpp"

#include <algorithm>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/transaction.hpp>
#include <userver/utils/datetime.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include "agent/protocol.hpp"
#include "agent/scan_handler.hpp"
#include "db/orders.hpp"
#include "db/scan.hpp"
#include "db/schema.hpp"

namespace factoryctl::agent {

namespace {

namespace json = userver::formats::json;
namespace pg = userver::storages::postgres;

auto alarmName(domain::AlarmKind kind) -> std::string_view {
  switch (kind) {
    case domain::AlarmKind::kLost:
      return "lost: sent, no result in time";
    case domain::AlarmKind::kComputingTooLong:
      return "AE computes the crafting plan too long";
    case domain::AlarmKind::kStalled:
      return "craft stalled: remaining does not go down";
  }
  return "alarm";
}

auto settingsFrom(const userver::components::ComponentConfig& config)
    -> domain::Settings {
  using std::chrono::seconds;
  domain::Settings s;
  s.poll = config["poll-interval"].As<seconds>(s.poll);
  s.scanEvery = config["scan-every"].As<seconds>(s.scanEvery);
  s.scanGiveUp = config["scan-give-up"].As<seconds>(s.scanGiveUp);
  s.timeouts.sent = config["sent-timeout"].As<seconds>(s.timeouts.sent);
  s.timeouts.computing =
      config["computing-timeout"].As<seconds>(s.timeouts.computing);
  s.timeouts.stalled =
      config["stalled-timeout"].As<seconds>(s.timeouts.stalled);
  s.lowFreeCpus = config["low-free-cpus"].As<std::int32_t>(s.lowFreeCpus);
  auto& p = s.scan;
  p.threshold = config["scan-threshold"].As<std::int64_t>(p.threshold);
  p.maxId = config["scan-max-id"].As<std::int32_t>(p.maxId);
  p.chunk = config["scan-chunk"].As<std::int32_t>(p.chunk);
  p.slowFrom = config["scan-slow-from"].As<std::int32_t>(p.slowFrom);
  p.slowTo = config["scan-slow-to"].As<std::int32_t>(p.slowTo);
  p.slowChunk = config["scan-slow-chunk"].As<std::int32_t>(p.slowChunk);
  return s;
}

}  // namespace

PollHandler::PollHandler(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()),
      settings_(settingsFrom(config)) {
  context.FindComponent<db::Schema>();
}

auto PollHandler::GetStaticConfigSchema() -> userver::yaml_config::Schema {
  return userver::yaml_config::MergeSchemas<HttpHandlerBase>(R"(
type: object
description: the agent's poll; periods like 10s, 5m
additionalProperties: false
properties:
    poll-interval: {type: string, description: how often the agent polls}
    scan-every: {type: string, description: time between scans}
    scan-give-up: {type: string, description: an unfinished scan this old is replaced}
    sent-timeout: {type: string, description: a sent order without a result becomes lost}
    computing-timeout: {type: string, description: alarm when AE computes longer}
    stalled-timeout: {type: string, description: alarm when a craft does not progress}
    low-free-cpus: {type: integer, description: low priority needs this many idle CPUs}
    scan-threshold: {type: integer, description: smaller stacks are scanned only if craftable}
    scan-max-id: {type: integer, description: highest item id the agent asks for}
    scan-chunk: {type: integer, description: ids per getItemsInNetworkById call}
    scan-slow-from: {type: integer, description: first id scanned in small chunks}
    scan-slow-to: {type: integer, description: last id scanned in small chunks}
    scan-slow-chunk: {type: integer, description: ids per call in the slow range}
)");
}

auto PollHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("text/plain; charset=utf-8");

  json::Value body;
  try {
    body = json::FromString(request.RequestBody());
  } catch (const json::Exception& e) {
    LOG_WARNING() << "poll: bad JSON: " << e.what();
    return replyLua(std::string("bad JSON: ") + e.what());
  }
  const auto report = parsePoll(body);
  if (!report) {
    LOG_WARNING() << "poll: " << report.error();
    return replyLua(report.error());
  }

  // Database errors are the server's fault: they end as HTTP 500, and the
  // agent simply polls again.
  const auto now = userver::utils::datetime::Now();
  const auto stock = db::currentStock(*db_);
  auto tx = db_->Begin(pg::ClusterHostType::kMaster, {});
  auto orders = db::loadOpenOrders(tx);
  const auto scans = db::scanState(tx);
  // The stock is as of the start of the last finished scan: goods whose order
  // closed since then wait for the next one.
  auto settling = scans.lastFinished ? db::loadSettling(tx, *scans.lastFinished)
                                     : std::vector<domain::ItemKey>{};
  const auto out = domain::tick(orders,
                                {.report = *report,
                                 .targets = db::loadTargets(tx),
                                 .stock = stock ? &*stock : nullptr,
                                 .settling = std::move(settling),
                                 .now = now},
                                settings_);
  db::saveOrders(tx, orders);
  const auto created = db::insertOrders(tx, out.created, now);
  db::saveCpus(tx, report->cpus, now);
  std::optional<ScanTask> scan;
  if (domain::needScan(scans, settings_, now)) {
    scan = ScanTask{db::startScan(tx, now), settings_.scan};
  }
  tx.Commit();

  for (const auto& e : out.errors) LOG_WARNING() << "poll: " << e;
  for (const auto& a : out.alarms) {
    LOG_ERROR() << "order " << a.order << ": " << alarmName(a.kind);
  }
  if (created > 0) LOG_INFO() << "poll: " << created << " new orders";
  if (scan) LOG_INFO() << "poll: scan " << scan->id << " started";

  PollReply reply{.poll = settings_.poll, .scan = scan};
  for (const auto& o : orders) {
    if (std::ranges::contains(out.send, o.id)) reply.send.push_back(o);
    if (std::ranges::contains(out.cancel, o.id, &domain::CancelTask::order)) {
      reply.cancel.push_back(o);
    }
  }
  const auto lua = replyPoll(reply);
  if (!lua) {
    LOG_ERROR() << "poll: reply: " << lua.error();
    return replyLua(lua.error());
  }
  return *lua;
}

}  // namespace factoryctl::agent
