#include "db/scan.hpp"

#include <format>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/io/chrono.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/events.hpp"

namespace factoryctl::db {

namespace {

constexpr auto kMaster = pg::ClusterHostType::kMaster;

// Locks the scan row for the rest of the transaction; fails unless the scan
// exists and runs.
auto lockRunning(pg::Transaction& tx, std::int64_t scan)
    -> std::expected<void, std::string> {
  const auto res = tx.Execute(
      "SELECT finished_at IS NULL FROM factoryctl.scans WHERE id = $1 "
      "FOR UPDATE",
      scan);
  if (res.IsEmpty()) return std::unexpected(std::format("no scan {}", scan));
  if (!res.AsSingleRow<bool>()) {
    return std::unexpected(std::format("scan {} is finished", scan));
  }
  return {};
}

}  // namespace

auto startScan(pg::Cluster& db, domain::TimePoint now) -> std::int64_t {
  return db
      .Execute(kMaster,
               "INSERT INTO factoryctl.scans (started_at) VALUES ($1) "
               "RETURNING id",
               pg::TimePointTz{now})
      .AsSingleRow<std::int64_t>();
}

auto startScan(pg::Transaction& tx, domain::TimePoint now) -> std::int64_t {
  return tx
      .Execute(
          "INSERT INTO factoryctl.scans (started_at) VALUES ($1) RETURNING id",
          pg::TimePointTz{now})
      .AsSingleRow<std::int64_t>();
}

auto storeBatch(pg::Cluster& db, std::int64_t scan,
                std::span<const domain::ScannedGoods> goods)
    -> std::expected<void, std::string> {
  std::vector<std::string> names;
  std::vector<std::string> variants;
  std::vector<std::string> labels;
  std::vector<std::int32_t> damages;
  std::vector<std::int64_t> sizes;
  std::vector<bool> craftable;
  for (const auto& g : goods) {
    names.push_back(g.key.name);
    damages.push_back(g.key.damage);
    variants.push_back(g.key.variant);
    labels.push_back(g.label);
    sizes.push_back(g.size);
    craftable.push_back(g.craftable);
  }

  auto tx = db.Begin(kMaster, {});
  if (auto ok = lockRunning(tx, scan); !ok) return ok;
  // A key appears at most once per batch (domain::mergeBatch): an upsert
  // cannot touch the same row twice.
  tx.Execute(
      "WITH batch AS ("
      "  SELECT * FROM unnest($2::text[], $3::integer[], $4::text[],"
      "                       $5::text[], $6::bigint[], $7::boolean[])"
      "    AS b(name, damage, variant, label, size, craftable)"
      "), g AS ("
      "  INSERT INTO factoryctl.goods (kind, name, damage, variant, label)"
      "  SELECT 'item', name, damage, variant, label FROM batch"
      "  ON CONFLICT (kind, name, damage, variant)"
      "    DO UPDATE SET label = EXCLUDED.label"
      "  RETURNING id, name, damage, variant"
      ") "
      "INSERT INTO factoryctl.stock (goods_id, amount, scan_id, craftable) "
      "SELECT g.id, batch.size, $1, batch.craftable FROM g "
      "JOIN batch USING (name, damage, variant) "
      "ON CONFLICT (goods_id) "
      "  DO UPDATE SET amount = EXCLUDED.amount, scan_id = EXCLUDED.scan_id,"
      "                craftable = EXCLUDED.craftable",
      scan, names, damages, variants, labels, sizes, craftable);
  tx.Commit();
  return {};
}

auto finishScan(pg::Cluster& db, std::int64_t scan, domain::TimePoint now)
    -> std::expected<void, std::string> {
  auto tx = db.Begin(kMaster, {});
  if (auto ok = lockRunning(tx, scan); !ok) return ok;
  tx.Execute("UPDATE factoryctl.scans SET finished_at = $2 WHERE id = $1", scan,
             pg::TimePointTz{now});
  // Stock not touched by this scan (or a later one) was not seen: 0.
  tx.Execute(
      "INSERT INTO factoryctl.stock_history (goods_id, at, amount) "
      "SELECT t.goods_id, $2,"
      "       CASE WHEN s.scan_id >= $1 THEN s.amount ELSE 0 END "
      "FROM factoryctl.targets t "
      "LEFT JOIN factoryctl.stock s USING (goods_id) "
      "ON CONFLICT DO NOTHING",
      scan, pg::TimePointTz{now});
  // Craftable goods are always scanned, even at 0: one this scan did not see
  // has lost its pattern. Kept apart from scan_id, which a probe moves up.
  tx.Execute(
      "UPDATE factoryctl.stock SET craftable = false "
      "WHERE scan_id < $1 AND craftable",
      scan);
  const auto seen = tx.Execute("SELECT count(*) FROM factoryctl.stock "
                               "WHERE scan_id = $1",
                               scan)
                        .AsSingleRow<std::int64_t>();
  const Event told{
      .at = now,
      .source = "scan",
      .text = std::format("scan {} finished: {} goods seen", scan, seen)};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return {};
}

auto currentStock(pg::Cluster& db) -> std::optional<domain::Stock> {
  const auto last = db.Execute(kMaster,
                               "SELECT max(id) FROM factoryctl.scans "
                               "WHERE finished_at IS NOT NULL")
                        .AsSingleRow<std::optional<std::int64_t>>();
  if (!last) return std::nullopt;

  const auto rows = db.Execute(
      kMaster,
      "SELECT g.name, g.damage, g.variant, s.amount "
      "FROM factoryctl.stock s JOIN factoryctl.goods g ON g.id = s.goods_id "
      "WHERE s.scan_id >= $1",
      *last);
  domain::Stock stock;
  for (const auto& row : rows) {
    stock.emplace(domain::ItemKey{row["name"].As<std::string>(),
                                  row["damage"].As<std::int32_t>(),
                                  row["variant"].As<std::string>()},
                  row["amount"].As<std::int64_t>());
  }
  return stock;
}

}  // namespace factoryctl::db
