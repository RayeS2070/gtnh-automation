#include "db/targets.hpp"

#include <format>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/exceptions.hpp>
#include <userver/storages/postgres/io/optional.hpp>
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
      "       t.note, t.fill, t.group_id "
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
                  row["fill"].As<bool>(),
                  row["group_id"].As<std::optional<std::int64_t>>()};
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
               domain::TimePoint now, std::string_view by)
    -> std::expected<void, std::string> {
  // Checked here, not left to the table's CHECKs: an error, not an exception.
  if (s.threshold < 0) return std::unexpected("threshold must be >= 0");
  if (s.batch <= 0) return std::unexpected("batch must be > 0");
  auto tx = db.Begin(kMaster, {});
  try {
    const auto res = tx.Execute(
        "INSERT INTO factoryctl.targets"
        "  (goods_id, threshold, batch, priority, enabled, note, fill,"
        "   group_id, updated_at) "
        "SELECT id, $2, $3, $4::factoryctl.priority, $5, $6, $7, $8, now() "
        "FROM factoryctl.goods WHERE id = $1 "
        "ON CONFLICT (goods_id) DO UPDATE SET"
        "  threshold = EXCLUDED.threshold, batch = EXCLUDED.batch,"
        "  priority = EXCLUDED.priority, enabled = EXCLUDED.enabled,"
        "  note = EXCLUDED.note, fill = EXCLUDED.fill,"
        "  group_id = EXCLUDED.group_id, updated_at = now()",
        goodsId, s.threshold, s.batch, priorityName(s.priority), s.enabled,
        s.note, s.fill, s.group);
    if (res.RowsAffected() == 0) {
      return std::unexpected(std::format("no goods {}", goodsId));
    }
  } catch (const pg::ForeignKeyViolation&) {
    // The goods exist (the SELECT found them): it is the group.
    return std::unexpected(std::format("no group {}", s.group.value_or(0)));
  }
  const Event told{.at = now,
                   .source = "target",
                   .text = "target: " + describe(s) + byWho(by),
                   .goodsId = goodsId};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return {};
}

auto listGroups(pg::Cluster& db) -> std::vector<TargetGroup> {
  const auto rows = db.Execute(
      kMaster,
      "SELECT g.id, g.name, count(t.goods_id) AS targets "
      "FROM factoryctl.target_groups g "
      "LEFT JOIN factoryctl.targets t ON t.group_id = g.id "
      "GROUP BY g.id ORDER BY lower(g.name), g.id");
  std::vector<TargetGroup> out;
  for (const auto& row : rows) {
    out.push_back({row["id"].As<std::int64_t>(), row["name"].As<std::string>(),
                   row["targets"].As<std::int64_t>()});
  }
  return out;
}

auto addGroup(pg::Cluster& db, const std::string& name, domain::TimePoint now,
              std::string_view by) -> std::expected<std::int64_t, std::string> {
  if (name.empty()) return std::unexpected("the name is empty");
  try {
    auto tx = db.Begin(kMaster, {});
    const auto id =
        tx.Execute(
              "INSERT INTO factoryctl.target_groups (name) VALUES ($1) "
              "RETURNING id",
              name)
            .AsSingleRow<std::int64_t>();
    const Event told{.at = now,
                     .source = "target",
                     .text = "target group added: " + name + byWho(by)};
    addEvents(tx, {&told, 1});
    tx.Commit();
    return id;
  } catch (const pg::UniqueViolation&) {
    return std::unexpected(std::format("a group '{}' is there already", name));
  }
}

auto renameGroup(pg::Cluster& db, std::int64_t id, const std::string& name,
                 domain::TimePoint now, std::string_view by)
    -> std::expected<bool, std::string> {
  if (name.empty()) return std::unexpected("the name is empty");
  try {
    auto tx = db.Begin(kMaster, {});
    const auto old = tx.Execute(
        "WITH old AS (SELECT name FROM factoryctl.target_groups WHERE id = $1"
        "             FOR UPDATE) "
        "UPDATE factoryctl.target_groups g SET name = $2 FROM old "
        "WHERE g.id = $1 RETURNING old.name",
        id, name);
    if (old.IsEmpty()) return false;
    const Event told{.at = now,
                     .source = "target",
                     .text = std::format("target group renamed: {} -> {}",
                                         old.AsSingleRow<std::string>(), name) +
                             byWho(by)};
    addEvents(tx, {&told, 1});
    tx.Commit();
    return true;
  } catch (const pg::UniqueViolation&) {
    return std::unexpected(std::format("a group '{}' is there already", name));
  }
}

auto deleteGroup(pg::Cluster& db, std::int64_t id, domain::TimePoint now,
                 std::string_view by) -> bool {
  auto tx = db.Begin(kMaster, {});
  const auto res = tx.Execute(
      "DELETE FROM factoryctl.target_groups WHERE id = $1 RETURNING name", id);
  if (res.IsEmpty()) return false;
  const Event told{.at = now,
                   .source = "target",
                   .text = "target group deleted: " +
                           res.AsSingleRow<std::string>() + byWho(by)};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return true;
}

auto deleteTarget(pg::Cluster& db, std::int64_t goodsId, domain::TimePoint now,
                  std::string_view by) -> bool {
  auto tx = db.Begin(kMaster, {});
  if (tx.Execute("DELETE FROM factoryctl.targets WHERE goods_id = $1", goodsId)
          .RowsAffected() == 0) {
    return false;
  }
  const Event told{.at = now,
                   .source = "target",
                   .text = "target removed" + byWho(by),
                   .goodsId = goodsId};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return true;
}

}  // namespace factoryctl::db
