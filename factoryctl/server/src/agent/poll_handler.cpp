#include "agent/poll_handler.hpp"

#include <algorithm>
#include <format>
#include <userver/components/component_config.hpp>
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

#include "agent/protocol.hpp"
#include "agent/scan_handler.hpp"
#include "config/dynamic.hpp"
#include "db/events.hpp"
#include "db/mining.hpp"
#include "db/orders.hpp"
#include "db/scan.hpp"
#include "db/schema.hpp"
#include "db/targets.hpp"

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
    case domain::AlarmKind::kNoLongerCraftable:
      return "AE can no longer craft it (pattern gone?): the craft may hang, "
             "cancel it if it does";
  }
  return "alarm";
}

}  // namespace

PollHandler::PollHandler(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()),
      config_(context.FindComponent<userver::components::DynamicConfig>()
                  .GetSource()) {
  context.FindComponent<db::Schema>();
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
  const auto settings = config_.GetCopy(config::kPoll);
  auto stock = db::currentStock(*db_);
  auto tx = db_->Begin(pg::ClusterHostType::kMaster, {});
  auto orders = db::loadOpenOrders(tx);
  const auto scans = db::scanState(tx);

  // The agent's probes of the targets, measured after the CPUs: fresher than
  // any scan. One that found nothing where the scan saw some is not trusted.
  std::vector<db::ProbedAmount> probed;
  // What this poll tells people: written in its transaction.
  std::vector<db::Event> events;
  if (stock) {
    for (const auto& p : report->stock) {
      const auto it = stock->find(p.item);
      const auto scanned = it == stock->end() ? 0 : it->second;
      if (const auto amount = domain::trustedAmount(p, scanned)) {
        probed.push_back({p.item, *amount});
        (*stock)[p.item] = *amount;
      } else {
        const auto text = std::format(
            "probe found nothing where the scan saw {}; ignored", scanned);
        LOG_WARNING() << "poll: " << p.item.name << ":" << p.item.damage << " "
                      << p.item.variant << ": " << text;
        events.push_back({.at = now,
                          .level = "warning",
                          .source = "server",
                          .text = text,
                          .goods = p.item});
      }
    }
    db::applyProbes(tx, probed, now);
  }
  // Goods whose order closed after their stock was last measured (scan or
  // probe) wait for the next measurement.
  auto settling = scans.lastFinished ? db::loadSettling(tx, *scans.lastFinished)
                                     : std::vector<domain::ItemKey>{};
  const auto out = domain::tick(orders,
                                {.report = *report,
                                 .targets = db::loadTargets(tx),
                                 .stock = stock ? &*stock : nullptr,
                                 .settling = std::move(settling),
                                 .uncraftable = db::loadUncraftable(tx),
                                 .now = now},
                                settings);
  db::saveOrders(tx, orders);
  db::saveCraftability(tx, out.craftability);
  const auto created = db::insertOrders(tx, out.created, now);
  db::saveCpus(tx, report->cpus, now);
  std::optional<ScanTask> scan;
  if (domain::needScan(scans, settings, now)) {
    scan = ScanTask{db::startScan(tx, now), settings.scan};
  }

  for (const auto& l : report->log) {
    events.push_back({.at = now,
                      .level = l.level == "err"   ? "error"
                               : l.level == "wrn" ? "warning"
                               : l.level == "dbg" ? "debug"
                                                  : "info",
                      .source = "agent",
                      .text = l.text,
                      .agentUptime = l.t});
  }
  for (const auto& c : out.changes) {
    const bool bad = c.to == domain::OrderStatus::kFailed ||
                     c.to == domain::OrderStatus::kLost;
    events.push_back({.at = now,
                      .level = bad ? "warning" : "info",
                      .source = "order",
                      .text = c.text,
                      .order = c.order});
  }
  for (const auto& a : out.alarms) {
    events.push_back({.at = now,
                      .level = "error",
                      .source = "order",
                      .text = std::string(alarmName(a.kind)),
                      .order = a.order});
  }
  for (const auto& e : out.errors) {
    events.push_back(
        {.at = now, .level = "warning", .source = "server", .text = e});
  }
  for (const auto& c : created) {
    events.push_back(
        {.at = now,
         .source = "order",
         .text = std::format("queued {} for a target", c.order.amount),
         .order = c.id});
  }
  if (scan) {
    events.push_back({.at = now,
                      .source = "scan",
                      .text = std::format("scan {} started", scan->id)});
  }
  db::addEvents(tx, events);
  tx.Commit();

  // The agent's own log, after the commit: a poll that failed sends it again.
  for (const auto& l : report->log) {
    using userver::logging::Level;
    const auto level = l.level == "err"   ? Level::kError
                       : l.level == "wrn" ? Level::kWarning
                       : l.level == "dbg" ? Level::kDebug
                                          : Level::kInfo;
    // Tags, not text: in the JSON log `source` tells the agent's lines apart.
    LOG(level) << userver::logging::LogExtra{{"source", "agent"},
                                             {"agent_uptime", l.t}}
               << l.text;
  }
  for (const auto& e : out.errors) LOG_WARNING() << "poll: " << e;
  for (const auto& a : out.alarms) {
    LOG_ERROR() << "order " << a.order << ": " << alarmName(a.kind);
  }
  if (!created.empty()) {
    LOG_INFO() << "poll: " << created.size() << " new orders";
  }
  if (scan) LOG_INFO() << "poll: scan " << scan->id << " started";

  PollReply reply{.poll = settings.poll, .scan = scan};
  // Every enabled target is measured in the next poll, the mining ones too:
  // their products may be below the scan threshold (the scan never sees them).
  for (const auto& t : db::listTargets(*db_)) {
    if (t.settings.enabled) reply.probe.push_back({t.goods.key, t.goods.label});
  }
  for (const auto& t : db::listMiningTargets(*db_)) {
    const bool listed = std::ranges::any_of(
        reply.probe, [&](const auto& p) { return p.item == t.goods.key; });
    if (t.target.enabled && !listed) {
      reply.probe.push_back({t.goods.key, t.goods.label});
    }
  }
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
