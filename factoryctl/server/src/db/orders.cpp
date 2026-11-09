#include "db/orders.hpp"

#include <map>

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
                  domain::TimePoint now) -> std::vector<CreatedOrder> {
  std::vector<CreatedOrder> made;
  for (const auto& o : orders) {
    // orders_one_open: an open order of the goods makes this a no-op.
    const auto res = tx.Execute(
        "INSERT INTO factoryctl.orders"
        "  (goods_id, amount, source, created_at, changed_at) "
        "SELECT id, $4, 'auto', $5, $5 FROM factoryctl.goods "
        "WHERE kind = 'item' AND name = $1 AND damage = $2 AND variant = $3 "
        "ON CONFLICT DO NOTHING RETURNING id",
        o.item.name, o.item.damage, o.item.variant, o.amount,
        pg::TimePointTz{now});
    if (!res.IsEmpty()) made.push_back({res.AsSingleRow<std::int64_t>(), o});
  }
  return made;
}

auto loadTargets(pg::Transaction& tx) -> std::vector<domain::Target> {
  const auto rows = tx.Execute(
      "SELECT g.name, g.damage, g.variant, t.threshold, t.batch,"
      "       t.priority::text AS priority, t.fill "
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
                                                    : Priority::kHigh,
         row["fill"].As<bool>()});
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
      "LEFT JOIN factoryctl.stock s ON s.goods_id = g.id "
      "WHERE o.status IN ('done', 'failed', 'canceled', 'lost')"
      "  AND o.changed_at >= GREATEST($1, coalesce(s.probed_at, $1))",
      pg::TimePointTz{stockAsOf});
  std::vector<domain::ItemKey> keys;
  for (const auto& row : rows) {
    keys.push_back({row["name"].As<std::string>(),
                    row["damage"].As<std::int32_t>(),
                    row["variant"].As<std::string>()});
  }
  return keys;
}

auto loadUncraftable(pg::Transaction& tx) -> std::vector<domain::ItemKey> {
  const auto rows = tx.Execute(
      "SELECT g.name, g.damage, g.variant FROM factoryctl.stock s "
      "JOIN factoryctl.goods g ON g.id = s.goods_id WHERE NOT s.craftable");
  std::vector<domain::ItemKey> out;
  for (const auto& row : rows) {
    out.push_back({row["name"].As<std::string>(),
                   row["damage"].As<std::int32_t>(),
                   row["variant"].As<std::string>()});
  }
  return out;
}

auto saveCraftability(pg::Transaction& tx,
                      std::span<const domain::Craftability> learned) -> void {
  std::map<domain::ItemKey, bool> last;
  for (const auto& c : learned) last[c.item] = c.craftable;
  if (last.empty()) return;
  std::vector<std::string> names;
  std::vector<std::string> variants;
  std::vector<std::int32_t> damages;
  std::vector<bool> craftable;
  for (const auto& [item, value] : last) {
    names.push_back(item.name);
    damages.push_back(item.damage);
    variants.push_back(item.variant);
    craftable.push_back(value);
  }
  tx.Execute(
      "UPDATE factoryctl.stock s SET craftable = c.craftable "
      "FROM unnest($1::text[], $2::integer[], $3::text[], $4::boolean[])"
      "       AS c(name, damage, variant, craftable) "
      "JOIN factoryctl.goods g ON g.kind = 'item' AND g.name = c.name"
      "  AND g.damage = c.damage AND g.variant = c.variant "
      "WHERE s.goods_id = g.id",
      names, damages, variants, craftable);
}

auto applyProbes(pg::Transaction& tx, std::span<const ProbedAmount> probes,
                 domain::TimePoint now) -> void {
  if (probes.empty()) return;
  std::vector<std::string> names;
  std::vector<std::string> variants;
  std::vector<std::int32_t> damages;
  std::vector<std::int64_t> amounts;
  for (const auto& p : probes) {
    names.push_back(p.item.name);
    damages.push_back(p.item.damage);
    variants.push_back(p.item.variant);
    amounts.push_back(p.amount);
  }
  tx.Execute(
      "WITH last AS (SELECT max(id) AS id FROM factoryctl.scans"
      "              WHERE finished_at IS NOT NULL),"
      "     probe AS (SELECT * FROM unnest($1::text[], $2::integer[],"
      "                                    $3::text[], $4::bigint[])"
      "               AS p(name, damage, variant, amount)) "
      "INSERT INTO factoryctl.stock AS s (goods_id, amount, scan_id, probed_at,"
      "  craftable) "
      "SELECT g.id, probe.amount, coalesce(last.id, x.scan_id), $5, false "
      "FROM probe JOIN factoryctl.goods g ON g.kind = 'item'"
      "  AND g.name = probe.name AND g.damage = probe.damage"
      "  AND g.variant = probe.variant "
      "LEFT JOIN factoryctl.stock x ON x.goods_id = g.id, last "
      "WHERE coalesce(last.id, x.scan_id) IS NOT NULL "
      "ON CONFLICT (goods_id) DO UPDATE SET amount = EXCLUDED.amount,"
      "  probed_at = EXCLUDED.probed_at,"
      "  scan_id = GREATEST(s.scan_id, coalesce(EXCLUDED.scan_id, s.scan_id))",
      names, damages, variants, amounts, pg::TimePointTz{now});
}

}  // namespace factoryctl::db
