#include "db/orders.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <userver/storages/postgres/io/chrono.hpp>
#include <userver/storages/postgres/transaction.hpp>
#include <utility>

namespace factoryctl::db {

namespace {

using domain::OrderStatus;
using domain::Priority;

// Names of the database enums. The enums are read and written as text, so
// the C++ side needs no type registration.
constexpr std::array<std::pair<OrderStatus, std::string_view>, 9> kStatuses{{
    {OrderStatus::kQueued, "queued"},
    {OrderStatus::kSent, "sent"},
    {OrderStatus::kComputing, "computing"},
    {OrderStatus::kAccepted, "accepted"},
    {OrderStatus::kCrafting, "crafting"},
    {OrderStatus::kDone, "done"},
    {OrderStatus::kFailed, "failed"},
    {OrderStatus::kCanceled, "canceled"},
    {OrderStatus::kLost, "lost"},
}};

}  // namespace

auto loadOpenOrders(pg::Transaction& tx) -> std::vector<domain::OrderState> {
  const auto rows = tx.Execute(
      "SELECT o.id, g.name, g.damage, g.variant, g.label, o.amount,"
      "       o.status::text AS status, o.changed_at, o.message, o.cpu,"
      "       o.remaining, o.progress_at, o.cancel_requested, o.alarm_raised "
      "FROM factoryctl.orders o JOIN factoryctl.goods g ON g.id = o.goods_id "
      "WHERE o.status IN ('queued', 'sent', 'computing', 'accepted',"
      "                   'crafting') "
      "ORDER BY o.id "
      "FOR UPDATE OF o");
  std::vector<domain::OrderState> orders;
  for (const auto& row : rows) {
    domain::OrderState o;
    o.id = row["id"].As<std::int64_t>();
    o.item = {row["name"].As<std::string>(), row["damage"].As<std::int32_t>(),
              row["variant"].As<std::string>()};
    o.label = row["label"].As<std::string>();
    o.amount = row["amount"].As<std::int64_t>();
    o.status = statusOf(row["status"].As<std::string>());
    o.changedAt = row["changed_at"].As<pg::TimePointTz>().GetUnderlying();
    o.message = row["message"].As<std::string>();
    o.cpu = row["cpu"].As<std::string>();
    o.remaining = row["remaining"].As<std::int64_t>();
    if (const auto at = row["progress_at"].As<std::optional<pg::TimePointTz>>())
      o.progressAt = at->GetUnderlying();
    o.cancelRequested = row["cancel_requested"].As<bool>();
    o.alarmRaised = row["alarm_raised"].As<bool>();
    orders.push_back(std::move(o));
  }
  return orders;
}

auto saveOrders(pg::Transaction& tx, std::span<const domain::OrderState> orders)
    -> void {
  for (const auto& o : orders) {
    const std::optional<pg::TimePointTz> progress =
        o.progressAt == domain::TimePoint{}
            ? std::nullopt
            : std::optional{pg::TimePointTz{o.progressAt}};
    tx.Execute(
        "UPDATE factoryctl.orders SET"
        "  status = $2::factoryctl.order_status, changed_at = $3,"
        "  message = $4, cpu = $5, remaining = $6, progress_at = $7,"
        "  cancel_requested = $8, alarm_raised = $9 "
        "WHERE id = $1",
        o.id, statusName(o.status), pg::TimePointTz{o.changedAt}, o.message,
        o.cpu, o.remaining, progress, o.cancelRequested, o.alarmRaised);
  }
}

auto insertOrders(pg::Transaction& tx, std::span<const domain::NewOrder> orders,
                  domain::TimePoint now) -> std::int64_t {
  std::int64_t made = 0;
  for (const auto& o : orders) {
    // orders_one_open: an open order of the goods makes this a no-op.
    const auto res = tx.Execute(
        "INSERT INTO factoryctl.orders"
        "  (goods_id, amount, source, created_at, changed_at) "
        "SELECT id, $4, 'auto', $5, $5 FROM factoryctl.goods "
        "WHERE kind = 'item' AND name = $1 AND damage = $2 AND variant = $3 "
        "ON CONFLICT DO NOTHING",
        o.item.name, o.item.damage, o.item.variant, o.amount,
        pg::TimePointTz{now});
    made += static_cast<std::int64_t>(res.RowsAffected());
  }
  return made;
}

auto loadTargets(pg::Transaction& tx) -> std::vector<domain::Target> {
  const auto rows = tx.Execute(
      "SELECT g.name, g.damage, g.variant, t.threshold, t.batch,"
      "       t.priority::text AS priority "
      "FROM factoryctl.targets t JOIN factoryctl.goods g ON g.id = t.goods_id "
      "WHERE t.enabled ORDER BY g.id");
  std::vector<domain::Target> targets;
  for (const auto& row : rows) {
    targets.push_back(
        {{row["name"].As<std::string>(), row["damage"].As<std::int32_t>(),
          row["variant"].As<std::string>()},
         row["threshold"].As<std::int64_t>(),
         row["batch"].As<std::int64_t>(),
         row["priority"].As<std::string>() == "low" ? Priority::kLow
                                                    : Priority::kHigh});
  }
  return targets;
}

auto saveCpus(pg::Transaction& tx, std::span<const domain::Cpu> cpus,
              domain::TimePoint now) -> void {
  tx.Execute("DELETE FROM factoryctl.cpus");
  for (const auto& c : cpus) {
    const auto& k = c.crafting;
    // goods_id stays NULL for a craft of goods no scan has seen yet.
    tx.Execute(
        "INSERT INTO factoryctl.cpus (name, busy, goods_id, remaining, "
        "seen_at) "
        "VALUES ($1, $2,"
        "  (SELECT id FROM factoryctl.goods WHERE kind = 'item'"
        "   AND name = $3 AND damage = $4 AND variant = $5),"
        "  $6, $7)",
        c.name, c.busy, k ? k->name : std::string(), k ? k->damage : 0,
        k ? k->variant : std::string(), c.remaining, pg::TimePointTz{now});
  }
}

auto scanState(pg::Transaction& tx) -> domain::ScanState {
  // The row refers into the result set: keep it alive.
  const auto res = tx.Execute(
      "SELECT"
      " (SELECT max(started_at) FROM factoryctl.scans"
      "  WHERE finished_at IS NOT NULL) AS finished,"
      " (SELECT max(started_at) FROM factoryctl.scans"
      "  WHERE finished_at IS NULL) AS running");
  const auto row = res.Front();
  domain::ScanState s;
  if (const auto t = row["finished"].As<std::optional<pg::TimePointTz>>())
    s.lastFinished = t->GetUnderlying();
  if (const auto t = row["running"].As<std::optional<pg::TimePointTz>>())
    s.running = t->GetUnderlying();
  return s;
}

auto statusName(OrderStatus s) -> std::string {
  for (const auto& [status, name] : kStatuses) {
    if (status == s) return std::string(name);
  }
  return "failed";
}

auto statusOf(std::string_view name) -> OrderStatus {
  for (const auto& [status, n] : kStatuses) {
    if (n == name) return status;
  }
  // The schema allows nothing else: a mismatch is a bug, not data.
  throw std::logic_error("unknown order status " + std::string(name));
}

auto loadSettling(pg::Transaction& tx, domain::TimePoint stockAsOf)
    -> std::vector<domain::ItemKey> {
  const auto rows = tx.Execute(
      "SELECT DISTINCT g.name, g.damage, g.variant "
      "FROM factoryctl.orders o JOIN factoryctl.goods g ON g.id = o.goods_id "
      "WHERE o.status IN ('done', 'failed', 'canceled', 'lost')"
      "  AND o.changed_at >= $1",
      pg::TimePointTz{stockAsOf});
  std::vector<domain::ItemKey> keys;
  for (const auto& row : rows) {
    keys.push_back({row["name"].As<std::string>(),
                    row["damage"].As<std::int32_t>(),
                    row["variant"].As<std::string>()});
  }
  return keys;
}

}  // namespace factoryctl::db
