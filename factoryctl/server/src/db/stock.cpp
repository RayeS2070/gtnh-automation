#include "db/stock.hpp"

#include <userver/storages/postgres/cluster.hpp>

namespace factoryctl::db {

namespace {

auto kindName(GoodsKind kind) -> std::string {
  switch (kind) {
    case GoodsKind::kItems: return "items";
    case GoodsKind::kFluids: return "fluids";
    case GoodsKind::kAll: break;
  }
  return "all";
}

}  // namespace

auto findStock(pg::Cluster& db, const StockQuery& query)
    -> std::vector<StockRow> {
  // Only fixed fragments go into the text; the user's words are a parameter.
  const std::string dir = query.descending ? " DESC" : "";
  const std::string orderBy =
      (query.order == StockOrder::kAmount ? "amount" + dir + ", " : "") +
      "shown" + dir + ", id" + dir;
  // strpos, not LIKE: % and _ in a query are just characters.
  const auto rows = db.Execute(
      pg::ClusterHostType::kMaster,
      "WITH last AS (SELECT max(id) AS id FROM factoryctl.scans"
      "              WHERE finished_at IS NOT NULL) "
      "SELECT * FROM ("
      "  SELECT g.id, g.name, g.damage, g.variant, g.label,"
      "         CASE WHEN s.scan_id >= last.id THEN s.amount ELSE 0 END"
      "           AS amount, s.craftable,"
      "         lower(CASE WHEN g.variant <> ''"
      "                    THEN regexp_replace(g.label, '^drop of ', '')"
      "                    ELSE g.label END) AS shown"
      "  FROM factoryctl.goods g"
      "  JOIN factoryctl.stock s ON s.goods_id = g.id"
      "  CROSS JOIN last"
      "  WHERE last.id IS NOT NULL"
      "    AND ($2 = 'all' OR ($2 = 'fluids') = (g.variant <> ''))"
      ") found "
      "WHERE NOT EXISTS ("
      "  SELECT 1 FROM regexp_split_to_table($1, '\\s+') AS word"
      "  WHERE word <> '' AND strpos(shown, lower(word)) = 0) "
      "ORDER BY " +
          orderBy + " LIMIT $3",
      query.text, kindName(query.kind), query.limit);
  std::vector<StockRow> out;
  for (const auto& row : rows) {
    out.push_back(
        {row["id"].As<std::int64_t>(),
         {row["name"].As<std::string>(), row["damage"].As<std::int32_t>(),
          row["variant"].As<std::string>()},
         row["label"].As<std::string>(),
         row["amount"].As<std::int64_t>(), row["craftable"].As<bool>()});
  }
  return out;
}

}  // namespace factoryctl::db
