#include "db/bhc.hpp"

#include <chrono>
#include <format>
#include <userver/formats/json/value_builder.hpp>
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
    "name, x, y, z, run, status, seen_at, settings_version, settings";

auto read(const pg::Row& row) -> BhcSetup {
  BhcSetup s;
  s.name = row["name"].As<std::string>();
  const auto x = row["x"].As<std::optional<std::int64_t>>();
  const auto y = row["y"].As<std::optional<std::int64_t>>();
  const auto z = row["z"].As<std::optional<std::int64_t>>();
  if (x && y && z) s.coords = agent::Coords{*x, *y, *z};
  s.run = row["run"].As<std::string>();
  s.status = row["status"].As<json::Value>();
  s.seenAt = row["seen_at"].As<pg::TimePointTz>().GetUnderlying();
  s.settingsVersion = row["settings_version"].As<std::int64_t>();
  s.settings = row["settings"].As<std::optional<json::Value>>();
  return s;
}

}  // namespace

auto describeBhcSettings(const json::Value& s) -> std::string {
  return std::format(
      "hold at {}, floor {}, {} holds, {} L to open",
      s["holdAt"].As<std::int64_t>(0), s["floor"].As<std::int64_t>(0),
      s["maxHolds"].As<std::int64_t>(0), s["minSpacetime"].As<std::int64_t>(0));
}

auto lockBhcSetup(pg::Transaction& tx, const std::string& name)
    -> std::optional<BhcSetup> {
  const auto res = tx.Execute(
      std::format(
          "SELECT {} FROM factoryctl.bhc_setups WHERE name = $1 FOR UPDATE",
          kColumns),
      name);
  if (res.IsEmpty()) return std::nullopt;
  return read(res[0]);
}

auto saveBhcSetup(pg::Transaction& tx, const BhcSetup& s, domain::TimePoint now)
    -> void {
  const auto c = s.coords;
  tx.Execute(
      "INSERT INTO factoryctl.bhc_setups (name, x, y, z, run, status, seen_at,"
      "  settings_version, settings, settings_at) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9,"
      "  CASE WHEN $9::jsonb IS NULL THEN NULL ELSE $10 END) "
      "ON CONFLICT (name) DO UPDATE SET x = $2, y = $3, z = $4, run = $5,"
      "  status = $6, seen_at = $7,"
      "  settings_at = CASE WHEN factoryctl.bhc_setups.settings_version <> $8"
      "    THEN $10 ELSE factoryctl.bhc_setups.settings_at END,"
      "  settings_version = $8, settings = $9",
      s.name, c ? std::optional{c->x} : std::nullopt,
      c ? std::optional{c->y} : std::nullopt,
      c ? std::optional{c->z} : std::nullopt, s.run, s.status,
      pg::TimePointTz{s.seenAt}, s.settingsVersion, s.settings,
      pg::TimePointTz{now});
}

auto listBhcSetups(pg::Cluster& db) -> std::vector<BhcSetup> {
  std::vector<BhcSetup> out;
  for (const auto& row : db.Execute(
           kMaster,
           std::format("SELECT {} FROM factoryctl.bhc_setups ORDER BY name",
                       kColumns))) {
    out.push_back(read(row));
  }
  return out;
}

auto addBhcSessions(pg::Transaction& tx, const std::string& setup,
                    const std::string& run,
                    std::span<const agent::BhcSession> sessions,
                    domain::TimePoint now) -> std::vector<agent::BhcSession> {
  std::vector<agent::BhcSession> added;
  for (const auto& s : sessions) {
    const auto closedAt =
        now - std::chrono::duration_cast<domain::TimePoint::duration>(
                  std::chrono::duration<double>(s.ago));
    const auto res = tx.Execute(
        "INSERT INTO factoryctl.bhc_sessions (setup, run, session, closed_at,"
        "  seconds, holds, spent, recipes, ending, unknown, blocked) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11) "
        "ON CONFLICT DO NOTHING",
        setup, run, s.session, pg::TimePointTz{closedAt}, s.seconds, s.holds,
        s.spent, s.recipes, s.ending, s.unknown, s.blocked);
    if (res.RowsAffected() > 0) added.push_back(s);
  }
  if (!added.empty()) {
    tx.Execute(
        "DELETE FROM factoryctl.bhc_sessions WHERE setup = $1 AND closed_at < ("
        "  SELECT closed_at FROM factoryctl.bhc_sessions WHERE setup = $1"
        "  ORDER BY closed_at DESC OFFSET $2 LIMIT 1)",
        setup, kBhcSessionsKept - 1);
  }
  return added;
}

auto listBhcSessions(pg::Cluster& db, const std::string& setup,
                     std::int64_t limit) -> std::vector<BhcSessionRow> {
  std::vector<BhcSessionRow> out;
  for (const auto& row : db.Execute(
           kMaster,
           "SELECT session, closed_at, seconds, holds, spent, recipes, ending,"
           "  unknown, blocked FROM factoryctl.bhc_sessions WHERE setup = $1"
           "  ORDER BY closed_at DESC, session DESC LIMIT $2",
           setup, limit)) {
    BhcSessionRow r;
    r.closedAt = row["closed_at"].As<pg::TimePointTz>().GetUnderlying();
    r.s.session = row["session"].As<std::int64_t>();
    r.s.seconds = row["seconds"].As<std::int64_t>();
    r.s.holds = row["holds"].As<std::int64_t>();
    r.s.spent = row["spent"].As<std::int64_t>();
    r.s.recipes = row["recipes"].As<std::int64_t>();
    r.s.ending = row["ending"].As<std::string>();
    r.s.unknown = row["unknown"].As<bool>();
    r.s.blocked = row["blocked"].As<bool>();
    out.push_back(std::move(r));
  }
  return out;
}

auto setBhcSettings(pg::Cluster& db, const std::string& name,
                    const json::Value& settings, domain::TimePoint now,
                    std::string_view by)
    -> std::expected<std::optional<std::int64_t>, std::string> {
  if (auto ok = agent::checkBhcSettings(settings); !ok) {
    return std::unexpected(ok.error());
  }
  auto tx = db.Begin(kMaster, {});
  auto setup = lockBhcSetup(tx, name);
  if (!setup) return std::optional<std::int64_t>{};
  json::ValueBuilder clean(settings);
  clean.Remove("version");  // the column has it
  setup->settings = clean.ExtractValue();
  setup->settingsVersion += 1;
  saveBhcSetup(tx, *setup, now);
  const Event told{.at = now,
                   .source = "bhc",
                   .text = std::format("[{}] settings v{} from the client: {}",
                                       name, setup->settingsVersion,
                                       describeBhcSettings(*setup->settings)) +
                           byWho(by)};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return std::optional{setup->settingsVersion};
}

auto deleteBhcSetup(pg::Cluster& db, const std::string& name,
                    domain::TimePoint now, std::string_view by) -> bool {
  auto tx = db.Begin(kMaster, {});
  if (tx.Execute("DELETE FROM factoryctl.bhc_setups WHERE name = $1", name)
          .RowsAffected() == 0) {
    return false;
  }
  const Event told{
      .at = now,
      .source = "bhc",
      .text = std::format("[{}] setup forgotten", name) + byWho(by)};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return true;
}

}  // namespace factoryctl::db
