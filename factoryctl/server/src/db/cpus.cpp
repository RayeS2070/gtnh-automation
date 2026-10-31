#include "db/cpus.hpp"

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/io/chrono.hpp>

namespace factoryctl::db {

auto listCpus(pg::Cluster& db) -> std::vector<CpuRow> {
  // One open order per goods (orders_one_open): the join adds no rows.
  const auto rows = db.Execute(
      pg::ClusterHostType::kMaster,
      "WITH last AS (SELECT max(id) AS id FROM factoryctl.scans"
      "              WHERE finished_at IS NOT NULL) "
      "SELECT c.name, c.busy, c.remaining, c.seen_at,"
      "       g.id AS goods_id, g.name AS goods_name, g.damage, g.variant,"
      "       g.label,"
      "       CASE WHEN s.scan_id >= last.id THEN s.amount ELSE 0 END AS stock,"
      "       coalesce(s.craftable, false) AS craftable,"
      "       o.id AS order_id "
      "FROM factoryctl.cpus c "
      "LEFT JOIN factoryctl.goods g ON g.id = c.goods_id "
      "LEFT JOIN factoryctl.stock s ON s.goods_id = g.id "
      "LEFT JOIN factoryctl.orders o ON o.goods_id = g.id"
      "  AND o.status IN ('queued', 'sent', 'computing', 'accepted',"
      "                   'crafting') "
      "CROSS JOIN last "
      "ORDER BY c.name");
  std::vector<CpuRow> out;
  for (const auto& row : rows) {
    CpuRow c;
    c.name = row["name"].As<std::string>();
    c.busy = row["busy"].As<bool>();
    c.remaining = row["remaining"].As<std::int64_t>();
    c.seenAt = row["seen_at"].As<pg::TimePointTz>().GetUnderlying();
    if (const auto id = row["goods_id"].As<std::optional<std::int64_t>>()) {
      c.goods = StockRow{
          *id,
          {row["goods_name"].As<std::string>(), row["damage"].As<std::int32_t>(),
           row["variant"].As<std::string>()},
          row["label"].As<std::string>(),
          row["stock"].As<std::optional<std::int64_t>>().value_or(0),
          row["craftable"].As<bool>()};
    }
    c.order = row["order_id"].As<std::optional<std::int64_t>>();
    out.push_back(std::move(c));
  }
  return out;
}

}  // namespace factoryctl::db
