#include "db/schema.hpp"

#include <format>
#include <stdexcept>
#include <userver/components/component_context.hpp>
#include <userver/logging/log.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/transaction.hpp>
#include <userver/utils/datetime.hpp>

#include "db/events.hpp"
#include "db/sql.hpp"

namespace factoryctl::db {

namespace {

namespace pg = userver::storages::postgres;

// DDL as is, without the prepared statement cache.
const pg::CommandControl kNoPrepare{
    std::chrono::seconds{10}, std::chrono::seconds{10},
    pg::CommandControl::PreparedStatementsOptionOverride::kDisabled};

}  // namespace

auto ensureSchema(pg::Cluster& db)
    -> std::expected<SchemaOutcome, std::string> {
  const auto sql = schemaSql();
  const auto code = schemaVersion(sql);
  if (!code) return std::unexpected("schema.sql states no schema_version");

  const auto exists =
      db.Execute(pg::ClusterHostType::kMaster,
                 "SELECT to_regclass('factoryctl.schema_version') IS NOT NULL")
          .AsSingleRow<bool>();
  if (!exists) {
    auto tx = db.Begin(pg::ClusterHostType::kMaster, {});
    for (const auto& statement : splitStatements(sql)) {
      tx.Execute(kNoPrepare, statement);
    }
    tx.Commit();
    return SchemaOutcome::kCreated;
  }

  const auto version =
      db.Execute(pg::ClusterHostType::kMaster,
                 "SELECT version FROM factoryctl.schema_version")
          .AsSingleRow<int>();
  if (version != *code) {
    return std::unexpected(std::format(
        "database schema is v{}, this server needs v{}. The data is not kept "
        "across versions: run `DROP SCHEMA factoryctl CASCADE;` and restart",
        version, *code));
  }
  return SchemaOutcome::kReady;
}

Schema::Schema(const userver::components::ComponentConfig& config,
               const userver::components::ComponentContext& context)
    : ComponentBase(config, context) {
  const auto db =
      context.FindComponent<userver::components::Postgres>("postgres-db")
          .GetCluster();
  const auto outcome = ensureSchema(*db);
  // Component construction is how userver fails a start: it takes exceptions.
  if (!outcome) throw std::runtime_error(outcome.error());
  const auto text = *outcome == SchemaOutcome::kCreated
                        ? "server started: created the database schema"
                        : "server started";
  LOG_INFO() << text;
  auto tx = db->Begin(pg::ClusterHostType::kMaster, {});
  const Event told{.at = userver::utils::datetime::Now(),
                   .source = "server",
                   .text = text};
  addEvents(tx, {&told, 1});
  tx.Commit();
}

}  // namespace factoryctl::db
