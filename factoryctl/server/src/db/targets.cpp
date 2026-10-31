#include "db/targets.hpp"

#include <format>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/events.hpp"

namespace factoryctl::db {

namespace {

constexpr auto kMaster = pg::ClusterHostType::kMaster;

auto priorityName(domain::Priority p) -> std::string {
  return p == domain::Priority::kLow ? "low" : "high";
}

}  // namespace

auto listTargets(pg::Cluster& db) -> std::vector<TargetRow> {
  const auto rows = db.Execute(
      kMaster,
      "WITH last AS (SELECT max(id) AS id FROM factoryctl.scans"
      "              WHERE finished_at IS NOT NULL) "
      "SELECT g.id, g.name, g.damage, g.variant, g.label,"
      "       CASE WHEN s.scan_id >= last.id THEN s.amount ELSE 0 END AS "
      "amount, coalesce(s.craftable, false) AS craftable,"
      "       t.threshold, t.batch, t.priority::text AS priority, t.enabled,"
      "       t.note, t.fill "
      "FROM factoryctl.targets t "
      "JOIN factoryctl.goods g ON g.id = t.goods_id "
      "LEFT JOIN factoryctl.stock s ON s.goods_id = g.id "
      "CROSS JOIN last "
      "ORDER BY lower(g.label), g.id");
  std::vector<TargetRow> out;
  for (const auto& row : rows) {
    TargetRow t;
    t.goods = {row["id"].As<std::int64_t>(),
               {row["name"].As<std::string>(), row["damage"].As<std::int32_t>(),
                row["variant"].As<std::string>()},
               row["label"].As<std::string>(),
               row["amount"].As<std::optional<std::int64_t>>().value_or(0),
               row["craftable"].As<bool>()};
    t.settings = {row["threshold"].As<std::int64_t>(),
                  row["batch"].As<std::int64_t>(),
                  row["priority"].As<std::string>() == "low"
                      ? domain::Priority::kLow
                      : domain::Priority::kHigh,
                  row["enabled"].As<bool>(),
                  row["note"].As<std::string>(),
                  row["fill"].As<bool>()};
    out.push_back(std::move(t));
  }
  return out;
}

auto describe(const TargetSettings& s) -> std::string {
  auto text = s.threshold <= 0
                  ? std::format("order {} whenever nothing is on order", s.batch)
                  : std::format("keep {}, order {}", s.threshold, s.batch);
  if (s.priority == domain::Priority::kLow) text += ", low priority";
  if (s.fill) text += ", fill";
  if (!s.enabled) text += ", paused";
  return text;
}

auto setTarget(pg::Cluster& db, std::int64_t goodsId, const TargetSettings& s,
               domain::TimePoint now) -> std::expected<void, std::string> {
  // Checked here, not left to the table's CHECKs: an error, not an exception.
  if (s.threshold < 0) return std::unexpected("threshold must be >= 0");
  if (s.batch <= 0) return std::unexpected("batch must be > 0");
  auto tx = db.Begin(kMaster, {});
  const auto res = tx.Execute(
      "INSERT INTO factoryctl.targets"
      "  (goods_id, threshold, batch, priority, enabled, note, fill,"
      "   updated_at) "
      "SELECT id, $2, $3, $4::factoryctl.priority, $5, $6, $7, now() "
      "FROM factoryctl.goods WHERE id = $1 "
      "ON CONFLICT (goods_id) DO UPDATE SET"
      "  threshold = EXCLUDED.threshold, batch = EXCLUDED.batch,"
      "  priority = EXCLUDED.priority, enabled = EXCLUDED.enabled,"
      "  note = EXCLUDED.note, fill = EXCLUDED.fill, updated_at = now()",
      goodsId, s.threshold, s.batch, priorityName(s.priority), s.enabled,
      s.note, s.fill);
  if (res.RowsAffected() == 0) {
    return std::unexpected(std::format("no goods {}", goodsId));
  }
  const Event told{.at = now,
                   .source = "target",
                   .text = "target: " + describe(s),
                   .goodsId = goodsId};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return {};
}

auto deleteTarget(pg::Cluster& db, std::int64_t goodsId, domain::TimePoint now)
    -> bool {
  auto tx = db.Begin(kMaster, {});
  if (tx.Execute("DELETE FROM factoryctl.targets WHERE goods_id = $1", goodsId)
          .RowsAffected() == 0) {
    return false;
  }
  const Event told{.at = now,
                   .source = "target",
                   .text = "target removed",
                   .goodsId = goodsId};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return true;
}

}  // namespace factoryctl::db
