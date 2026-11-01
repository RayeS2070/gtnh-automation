#include "db/qft.hpp"

#include <format>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/io/chrono.hpp>
#include <userver/storages/postgres/io/json_types.hpp>
#include <userver/storages/postgres/io/optional.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/events.hpp"

namespace factoryctl::db {

namespace {

constexpr auto kMaster = pg::ClusterHostType::kMaster;
namespace json = userver::formats::json;

constexpr std::string_view kColumns =
    "name, x, y, z, run, catalog, status, seen_at, settings_version, settings";

auto read(const pg::Row& row) -> QftSetup {
  QftSetup s;
  s.name = row["name"].As<std::string>();
  const auto x = row["x"].As<std::optional<std::int64_t>>();
  const auto y = row["y"].As<std::optional<std::int64_t>>();
  const auto z = row["z"].As<std::optional<std::int64_t>>();
  if (x && y && z) s.coords = agent::Coords{*x, *y, *z};
  s.run = row["run"].As<std::string>();
  s.catalog = row["catalog"].As<json::Value>();
  s.status = row["status"].As<json::Value>();
  s.seenAt = row["seen_at"].As<pg::TimePointTz>().GetUnderlying();
  s.settingsVersion = row["settings_version"].As<std::int64_t>();
  s.settings = row["settings"].As<std::optional<json::Value>>();
  return s;
}

}  // namespace

auto lockQftSetup(pg::Transaction& tx, const std::string& name)
    -> std::optional<QftSetup> {
  const auto res = tx.Execute(
      std::format("SELECT {} FROM factoryctl.qft_setups WHERE name = $1 FOR UPDATE",
                  kColumns),
      name);
  if (res.IsEmpty()) return std::nullopt;
  return read(res[0]);
}

auto saveQftSetup(pg::Transaction& tx, const QftSetup& s, domain::TimePoint now)
    -> void {
  const auto c = s.coords;
  tx.Execute(
      "INSERT INTO factoryctl.qft_setups (name, x, y, z, run, catalog, status,"
      "  seen_at, settings_version, settings, settings_at) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10,"
      "  CASE WHEN $10::jsonb IS NULL THEN NULL ELSE $11 END) "
      "ON CONFLICT (name) DO UPDATE SET x = $2, y = $3, z = $4, run = $5,"
      "  catalog = $6, status = $7, seen_at = $8,"
      "  settings_at = CASE WHEN factoryctl.qft_setups.settings_version <> $9"
      "    THEN $11 ELSE factoryctl.qft_setups.settings_at END,"
      "  settings_version = $9, settings = $10",
      s.name, c ? std::optional{c->x} : std::nullopt,
      c ? std::optional{c->y} : std::nullopt, c ? std::optional{c->z} : std::nullopt,
      s.run, s.catalog, s.status, pg::TimePointTz{s.seenAt}, s.settingsVersion,
      s.settings, pg::TimePointTz{now});
}

auto listQftSetups(pg::Cluster& db) -> std::vector<QftSetup> {
  std::vector<QftSetup> out;
  for (const auto& row : db.Execute(
           kMaster, std::format("SELECT {} FROM factoryctl.qft_setups ORDER BY name",
                                kColumns))) {
    out.push_back(read(row));
  }
  return out;
}

auto setQftSettings(pg::Cluster& db, const std::string& name,
                    const json::Value& settings, domain::TimePoint now)
    -> std::expected<std::optional<std::int64_t>, std::string> {
  auto tx = db.Begin(kMaster, {});
  auto setup = lockQftSetup(tx, name);
  if (!setup) return std::optional<std::int64_t>{};
  if (auto ok = agent::checkQftSettings(settings, setup->catalog); !ok) {
    return std::unexpected(ok.error());
  }
  json::ValueBuilder clean(settings);
  clean.Remove("version");  // the column has it
  setup->settings = clean.ExtractValue();
  setup->settingsVersion += 1;
  saveQftSetup(tx, *setup, now);
  const Event told{
      .at = now,
      .source = "qft",
      .text = std::format("[{}] settings v{} from the client: mode {}", name,
                          setup->settingsVersion,
                          settings["mode"].As<std::string>("?"))};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return std::optional{setup->settingsVersion};
}

auto deleteQftSetup(pg::Cluster& db, const std::string& name,
                    domain::TimePoint now) -> bool {
  auto tx = db.Begin(kMaster, {});
  if (tx.Execute("DELETE FROM factoryctl.qft_setups WHERE name = $1", name)
          .RowsAffected() == 0) {
    return false;
  }
  const Event told{.at = now,
                   .source = "qft",
                   .text = std::format("[{}] setup forgotten", name)};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return true;
}

}  // namespace factoryctl::db
