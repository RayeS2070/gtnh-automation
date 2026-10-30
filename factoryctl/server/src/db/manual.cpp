#include "db/manual.hpp"

#include <format>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/exceptions.hpp>
#include <userver/storages/postgres/io/chrono.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/orders.hpp"

namespace factoryctl::db {

namespace {

constexpr auto kMaster = pg::ClusterHostType::kMaster;

}  // namespace

auto orderOnce(pg::Cluster& db, std::int64_t goodsId, std::int64_t amount,
               std::string_view requestedBy, domain::TimePoint now)
    -> std::expected<std::int64_t, std::string> {
  if (amount <= 0) return std::unexpected("amount must be > 0");
  try {
    const auto res = db.Execute(
        kMaster,
        "INSERT INTO factoryctl.orders"
        "  (goods_id, amount, source, requested_by, created_at, changed_at) "
        "SELECT id, $2, 'manual', $3, $4, $4 FROM factoryctl.goods "
        "WHERE id = $1 RETURNING id",
        goodsId, amount, std::string(requestedBy), pg::TimePointTz{now});
    if (res.IsEmpty()) {
      return std::unexpected(std::format("no goods {}", goodsId));
    }
    return res.AsSingleRow<std::int64_t>();
  } catch (const pg::UniqueViolation&) {
    // orders_one_open: the CPU running an order is found by its item.
    return std::unexpected("already on order: wait for it or cancel it");
  }
}

auto listOrders(pg::Cluster& db, std::int64_t limit) -> std::vector<OrderRow> {
  const auto rows = db.Execute(
      kMaster,
      "WITH last AS (SELECT max(id) AS id FROM factoryctl.scans"
      "              WHERE finished_at IS NOT NULL) "
      "SELECT o.id, g.id AS goods_id, g.name, g.damage, g.variant, g.label,"
      "       CASE WHEN s.scan_id >= last.id THEN s.amount ELSE 0 END AS stock,"
      "       o.amount, o.status::text AS status, o.source::text AS source,"
      "       o.requested_by, o.created_at, o.changed_at, o.message, o.cpu,"
      "       o.remaining, o.cancel_requested "
      "FROM factoryctl.orders o "
      "JOIN factoryctl.goods g ON g.id = o.goods_id "
      "LEFT JOIN factoryctl.stock s ON s.goods_id = g.id "
      "CROSS JOIN last "
      "ORDER BY o.created_at DESC, o.id DESC LIMIT $1",
      limit);
  std::vector<OrderRow> out;
  for (const auto& row : rows) {
    OrderRow o;
    o.id = row["id"].As<std::int64_t>();
    o.goods = {row["goods_id"].As<std::int64_t>(),
               {row["name"].As<std::string>(), row["damage"].As<std::int32_t>(),
                row["variant"].As<std::string>()},
               row["label"].As<std::string>(),
               row["stock"].As<std::optional<std::int64_t>>().value_or(0)};
    o.amount = row["amount"].As<std::int64_t>();
    o.status = row["status"].As<std::string>();
    o.source = row["source"].As<std::string>();
    o.requestedBy = row["requested_by"].As<std::string>();
    o.createdAt = row["created_at"].As<pg::TimePointTz>().GetUnderlying();
    o.changedAt = row["changed_at"].As<pg::TimePointTz>().GetUnderlying();
    o.message = row["message"].As<std::string>();
    o.cpu = row["cpu"].As<std::string>();
    o.remaining = row["remaining"].As<std::int64_t>();
    o.cancelRequested = row["cancel_requested"].As<bool>();
    out.push_back(std::move(o));
  }
  return out;
}

auto cancelOrder(pg::Cluster& db, std::int64_t id, domain::TimePoint now)
    -> std::expected<domain::CancelAction, std::string> {
  // Locked like the poll locks open orders: one waits for the other.
  auto tx = db.Begin(kMaster, {});
  const auto res = tx.Execute(
      "SELECT status::text AS status, changed_at, cancel_requested "
      "FROM factoryctl.orders WHERE id = $1 FOR UPDATE",
      id);
  if (res.IsEmpty()) return std::unexpected(std::format("no order {}", id));
  const auto row = res.Front();
  domain::OrderState o;
  o.id = id;
  o.status = statusOf(row["status"].As<std::string>());
  o.changedAt = row["changed_at"].As<pg::TimePointTz>().GetUnderlying();
  o.cancelRequested = row["cancel_requested"].As<bool>();

  const auto action = domain::requestCancel(o, now);
  if (action != domain::CancelAction::kNone) {
    tx.Execute(
        "UPDATE factoryctl.orders SET status = $2::factoryctl.order_status,"
        "  changed_at = $3, cancel_requested = $4 WHERE id = $1",
        id, statusName(o.status), pg::TimePointTz{o.changedAt},
        o.cancelRequested);
  }
  tx.Commit();
  return action;
}

}  // namespace factoryctl::db
