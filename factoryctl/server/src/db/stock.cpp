#include "db/stock.hpp"

#include <userver/storages/postgres/cluster.hpp>

namespace factoryctl::db {

auto findStock(pg::Cluster& db, std::string_view query, std::int64_t limit)
    -> std::vector<StockRow> {
  // strpos, not LIKE: % and _ in a query are just characters.
  const auto rows = db.Execute(
      pg::ClusterHostType::kMaster,
      "WITH last AS (SELECT max(id) AS id FROM factoryctl.scans"
      "              WHERE finished_at IS NOT NULL) "
      "SELECT g.id, g.name, g.damage, g.variant, g.label,"
      "       CASE WHEN s.scan_id >= last.id THEN s.amount ELSE 0 END AS "
      "amount "
      "FROM factoryctl.goods g "
      "JOIN factoryctl.stock s ON s.goods_id = g.id "
      "CROSS JOIN last "
      "WHERE last.id IS NOT NULL AND strpos(lower(g.label), lower($1)) > 0 "
      "ORDER BY lower(g.label), g.id LIMIT $2",
      std::string(query), limit);
  std::vector<StockRow> out;
  for (const auto& row : rows) {
    out.push_back(
        {row["id"].As<std::int64_t>(),
         {row["name"].As<std::string>(), row["damage"].As<std::int32_t>(),
          row["variant"].As<std::string>()},
         row["label"].As<std::string>(),
         row["amount"].As<std::int64_t>()});
  }
  return out;
}

}  // namespace factoryctl::db
