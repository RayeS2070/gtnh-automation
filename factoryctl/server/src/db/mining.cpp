#include "db/mining.hpp"

#include <format>
#include <set>
#include <userver/formats/json/value_builder.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/exceptions.hpp>
#include <userver/storages/postgres/io/chrono.hpp>
#include <userver/storages/postgres/io/json_types.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/events.hpp"
#include "db/targets.hpp"

namespace factoryctl::db {

namespace {

constexpr auto kMaster = pg::ClusterHostType::kMaster;
namespace json = userver::formats::json;

constexpr std::string_view kSetupColumns = "name, run, status, plan, seen_at";

auto readSetup(const pg::Row& row) -> MiningSetup {
  return {.name = row["name"].As<std::string>(),
          .run = row["run"].As<std::string>(),
          .status = row["status"].As<json::Value>(),
          .plan = row["plan"].As<json::Value>(),
          .seenAt = row["seen_at"].As<pg::TimePointTz>().GetUnderlying()};
}

auto oresOf(const json::Value& v) -> std::vector<domain::ItemKey> {
  std::vector<domain::ItemKey> out;
  for (const auto& o : v) {
    out.push_back({.name = o["name"].As<std::string>(""),
                   .damage = o["damage"].As<std::int32_t>(0)});
  }
  return out;
}

auto oresJson(const std::vector<domain::ItemKey>& ores) -> json::Value {
  json::ValueBuilder b(json::Type::kArray);
  for (const auto& o : ores) {
    json::ValueBuilder e;
    e["name"] = o.name;
    e["damage"] = o.damage;
    b.PushBack(std::move(e));
  }
  return b.ExtractValue();
}

template <typename Executor>
auto targets(Executor& ex) -> std::vector<MiningTargetRow> {
  constexpr std::string_view kSql =
      "SELECT t.goods_id, g.name, g.damage, g.variant, g.label,"
      "  coalesce(s.amount, 0) AS amount, coalesce(s.craftable, false) AS"
      "  craftable, t.threshold, t.priority, t.enabled, t.ores "
      "FROM factoryctl.mining_targets t "
      "JOIN factoryctl.goods g ON g.id = t.goods_id "
      "LEFT JOIN factoryctl.stock s ON s.goods_id = t.goods_id "
      "ORDER BY lower(g.label), t.goods_id";
  pg::ResultSet rows = [&] {
    if constexpr (std::is_same_v<Executor, pg::Cluster>) {
      return ex.Execute(kMaster, std::string(kSql));
    } else {
      return ex.Execute(std::string(kSql));
    }
  }();
  std::vector<MiningTargetRow> out;
  for (const auto& row : rows) {
    MiningTargetRow r;
    r.goods = {.goodsId = row["goods_id"].As<std::int64_t>(),
               .key = {.name = row["name"].As<std::string>(),
                       .damage = row["damage"].As<std::int32_t>(),
                       .variant = row["variant"].As<std::string>()},
               .label = row["label"].As<std::string>(),
               .amount = row["amount"].As<std::int64_t>(),
               .craftable = row["craftable"].As<bool>()};
    r.target = {.product = r.goods.key,
                .threshold = static_cast<double>(row["threshold"].As<std::int64_t>()),
                .priority = row["priority"].As<double>(),
                .enabled = row["enabled"].As<bool>(),
                .ores = oresOf(row["ores"].As<json::Value>())};
    out.push_back(std::move(r));
  }
  return out;
}

auto describe(const MiningTargetSettings& s) -> std::string {
  auto text = std::format("keep {}, priority {}, {} ore{}", s.threshold,
                          s.priority, s.ores.size(), s.ores.size() == 1 ? "" : "s");
  if (!s.enabled) text += ", paused";
  return text;
}

}  // namespace

auto lockMiningSetup(pg::Transaction& tx, const std::string& name)
    -> std::optional<MiningSetup> {
  const auto res = tx.Execute(
      std::format("SELECT {} FROM factoryctl.mining_setups WHERE name = $1 "
                  "FOR UPDATE",
                  kSetupColumns),
      name);
  if (res.IsEmpty()) return std::nullopt;
  return readSetup(res[0]);
}

auto saveMiningSetup(pg::Transaction& tx, const MiningSetup& s) -> void {
  tx.Execute(
      "INSERT INTO factoryctl.mining_setups (name, run, status, plan, seen_at) "
      "VALUES ($1, $2, $3, $4, $5) ON CONFLICT (name) DO UPDATE SET run = $2,"
      "  status = $3, plan = $4, seen_at = $5",
      s.name, s.run, s.status, s.plan, pg::TimePointTz{s.seenAt});
}

auto listMiningSetups(pg::Transaction& tx, std::string_view except)
    -> std::vector<MiningSetup> {
  std::vector<MiningSetup> out;
  for (const auto& row :
       tx.Execute(std::format("SELECT {} FROM factoryctl.mining_setups "
                              "WHERE name <> $1 ORDER BY name",
                              kSetupColumns),
                  std::string(except))) {
    out.push_back(readSetup(row));
  }
  return out;
}

auto listMiningSetups(pg::Cluster& db) -> std::vector<MiningSetup> {
  std::vector<MiningSetup> out;
  for (const auto& row : db.Execute(
           kMaster, std::format("SELECT {} FROM factoryctl.mining_setups "
                                "ORDER BY name",
                                kSetupColumns))) {
    out.push_back(readSetup(row));
  }
  return out;
}

auto deleteMiningSetup(pg::Cluster& db, const std::string& name,
                       domain::TimePoint now, std::string_view by) -> bool {
  auto tx = db.Begin(kMaster, {});
  if (tx.Execute("DELETE FROM factoryctl.mining_setups WHERE name = $1", name)
          .RowsAffected() == 0) {
    return false;
  }
  const Event told{.at = now,
                   .source = "mining",
                   .text = std::format("[{}] setup forgotten", name) + byWho(by)};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return true;
}

auto listMiningTargets(pg::Transaction& tx) -> std::vector<MiningTargetRow> {
  return targets(tx);
}

auto listMiningTargets(pg::Cluster& db) -> std::vector<MiningTargetRow> {
  return targets(db);
}

auto ensureGoods(pg::Transaction& tx, const domain::ItemKey& key,
                 const std::string& label) -> std::int64_t {
  // As the scan writes goods: every kind is 'item', a fluid by its variant.
  return tx
      .Execute(
          "INSERT INTO factoryctl.goods (kind, name, damage, variant, label) "
          "VALUES ('item', $1, $2, $3, $4) ON CONFLICT (kind, name, damage, variant) "
          "DO UPDATE SET label = factoryctl.goods.label RETURNING id",
          key.name, key.damage, key.variant, label)
      .AsSingleRow<std::int64_t>();
}

namespace {

auto upsertTarget(pg::Transaction& tx, std::int64_t goodsId,
                  const MiningTargetSettings& s, domain::TimePoint now,
                  std::string_view by) -> std::expected<void, std::string> {
  try {
    const auto res = tx.Execute(
        "INSERT INTO factoryctl.mining_targets (goods_id, threshold, priority,"
        "  enabled, ores) SELECT id, $2, $3, $4, $5 FROM factoryctl.goods "
        "WHERE id = $1 ON CONFLICT (goods_id) DO UPDATE SET threshold = $2,"
        "  priority = $3, enabled = $4, ores = $5, updated_at = now()",
        goodsId, s.threshold, s.priority, s.enabled, oresJson(s.ores));
    if (res.RowsAffected() == 0) {
      return std::unexpected(std::format("no goods {}", goodsId));
    }
  } catch (const pg::CheckViolation&) {
    return std::unexpected("bad numbers");
  }
  const Event told{.at = now,
                   .source = "mining",
                   .text = "mining target: " + describe(s) + byWho(by),
                   .goodsId = goodsId};
  addEvents(tx, {&told, 1});
  return {};
}

auto checkTarget(const MiningTargetSettings& s) -> std::expected<void, std::string> {
  if (s.threshold <= 0) return std::unexpected("threshold must be > 0");
  if (!(s.priority > 0)) return std::unexpected("priority must be > 0");
  if (s.ores.empty()) return std::unexpected("no ore: what is it mined from?");
  return {};
}

}  // namespace

auto setMiningTarget(pg::Cluster& db, std::int64_t goodsId,
                     const MiningTargetSettings& s, domain::TimePoint now,
                     std::string_view by) -> std::expected<void, std::string> {
  if (auto ok = checkTarget(s); !ok) return ok;
  auto tx = db.Begin(kMaster, {});
  if (auto ok = upsertTarget(tx, goodsId, s, now, by); !ok) return ok;
  tx.Commit();
  return {};
}

auto addMiningTarget(pg::Cluster& db, const domain::ItemKey& product,
                     const std::string& label, const MiningTargetSettings& s,
                     domain::TimePoint now, std::string_view by)
    -> std::expected<std::int64_t, std::string> {
  if (auto ok = checkTarget(s); !ok) return std::unexpected(ok.error());
  if (product.name.empty()) return std::unexpected("no product");
  auto tx = db.Begin(kMaster, {});
  const auto id = ensureGoods(tx, product, label.empty() ? product.name : label);
  if (auto ok = upsertTarget(tx, id, s, now, by); !ok) {
    return std::unexpected(ok.error());
  }
  tx.Commit();
  return id;
}

auto deleteMiningTarget(pg::Cluster& db, std::int64_t goodsId,
                        domain::TimePoint now, std::string_view by) -> bool {
  auto tx = db.Begin(kMaster, {});
  if (tx.Execute("DELETE FROM factoryctl.mining_targets WHERE goods_id = $1",
                 goodsId)
          .RowsAffected() == 0) {
    return false;
  }
  const Event told{.at = now,
                   .source = "mining",
                   .text = "mining target removed" + byWho(by),
                   .goodsId = goodsId};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return true;
}

auto goodsById(pg::Cluster& db, std::int64_t id) -> std::optional<StockRow> {
  const auto res = db.Execute(
      kMaster,
      "SELECT g.id, g.name, g.damage, g.variant, g.label,"
      "  coalesce(s.amount, 0) AS amount, coalesce(s.craftable, false) AS craftable "
      "FROM factoryctl.goods g LEFT JOIN factoryctl.stock s ON s.goods_id = g.id "
      "WHERE g.id = $1",
      id);
  if (res.IsEmpty()) return std::nullopt;
  const auto row = res[0];
  return StockRow{.goodsId = row["id"].As<std::int64_t>(),
                  .key = {.name = row["name"].As<std::string>(),
                          .damage = row["damage"].As<std::int32_t>(),
                          .variant = row["variant"].As<std::string>()},
                  .label = row["label"].As<std::string>(),
                  .amount = row["amount"].As<std::int64_t>(),
                  .craftable = row["craftable"].As<bool>()};
}

auto fluidStock(pg::Transaction& tx, const std::vector<std::string>& fluids)
    -> std::map<std::string, double> {
  std::map<std::string, double> out;
  for (const auto& row : tx.Execute(
           "SELECT g.variant, s.amount FROM factoryctl.goods g "
           "JOIN factoryctl.stock s ON s.goods_id = g.id "
           "WHERE g.name = 'ae2fc:fluid_drop' AND g.variant = ANY($1)",
           fluids)) {
    out[row["variant"].As<std::string>()] =
        static_cast<double>(row["amount"].As<std::int64_t>());
  }
  return out;
}

auto ensureMiningSupplies(pg::Transaction& tx, std::span<const MiningSupply> supplies,
                          domain::TimePoint now) -> std::vector<Event> {
  std::vector<Event> out;
  if (supplies.empty()) return out;
  std::set<domain::ItemKey> seen;
  for (const auto& row : tx.Execute(
           "SELECT g.name, g.damage, g.variant FROM factoryctl.mining_supplies m "
           "JOIN factoryctl.goods g ON g.id = m.goods_id")) {
    seen.insert({row["name"].As<std::string>(), row["damage"].As<std::int32_t>(),
                 row["variant"].As<std::string>()});
  }
  std::optional<std::int64_t> group;
  for (const auto& s : supplies) {
    if (seen.contains(s.item.key)) continue;
    const auto id = ensureGoods(tx, s.item.key, s.item.label);
    tx.Execute("INSERT INTO factoryctl.mining_supplies (goods_id) VALUES ($1) "
               "ON CONFLICT DO NOTHING",
               id);
    if (!tx.Execute("SELECT 1 FROM factoryctl.targets WHERE goods_id = $1", id)
             .IsEmpty()) {
      continue;  // the player's, as it is
    }
    if (!group) {
      group = tx.Execute(
                    "INSERT INTO factoryctl.target_groups (name) VALUES ('Mining') "
                    "ON CONFLICT (name) DO UPDATE SET name = EXCLUDED.name "
                    "RETURNING id")
                  .AsSingleRow<std::int64_t>();
    }
    const TargetSettings t{.threshold = s.threshold,
                           .batch = s.batch,
                           .note = "for mining",
                           .group = group};
    tx.Execute(
        "INSERT INTO factoryctl.targets (goods_id, threshold, batch, note, group_id) "
        "VALUES ($1, $2, $3, $4, $5)",
        id, t.threshold, t.batch, t.note, t.group);
    out.push_back({.at = now,
                   .source = "target",
                   .text = "target: " + describe(t) + byWho("mining"),
                   .goodsId = id});
  }
  return out;
}

auto miningStock(pg::Transaction& tx, std::span<const domain::ItemKey> keys)
    -> std::map<domain::ItemKey, domain::MiningStock> {
  std::map<domain::ItemKey, domain::MiningStock> out;
  if (keys.empty()) return out;
  std::vector<std::string> names, variants;
  std::vector<std::int32_t> damages;
  for (const auto& k : keys) {
    names.push_back(k.name);
    damages.push_back(k.damage);
    variants.push_back(k.variant);
  }
  for (const auto& row : tx.Execute(
           "WITH last AS (SELECT max(id) AS id FROM factoryctl.scans"
           "              WHERE finished_at IS NOT NULL) "
           "SELECT g.name, g.damage, g.variant,"
           "       CASE WHEN s.scan_id >= last.id THEN s.amount ELSE 0 END AS amount,"
           "       coalesce(s.craftable, false) AS craftable "
           "FROM unnest($1::text[], $2::integer[], $3::text[]) AS k(name, damage, variant) "
           "JOIN factoryctl.goods g USING (name, damage, variant) "
           "LEFT JOIN factoryctl.stock s ON s.goods_id = g.id CROSS JOIN last",
           names, damages, variants)) {
    out[{row["name"].As<std::string>(), row["damage"].As<std::int32_t>(),
         row["variant"].As<std::string>()}] = {
        .amount = static_cast<double>(
            row["amount"].As<std::optional<std::int64_t>>().value_or(0)),
        .craftable = row["craftable"].As<bool>()};
  }
  return out;
}

}  // namespace factoryctl::db
