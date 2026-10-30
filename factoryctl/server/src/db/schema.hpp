#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <userver/components/component_base.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::db {

enum class SchemaOutcome { kCreated, kReady };

// Creates the schema of schema/schema.sql in a database without one, accepts
// one of the same version, refuses another version (with how to drop it).
// Database errors come as userver exceptions.
auto ensureSchema(userver::storages::postgres::Cluster& db)
    -> std::expected<SchemaOutcome, std::string>;

// Runs ensureSchema() on start: components that use the database depend on
// this one. The data is not worth migrations, a refused start says how to
// drop the old schema.
class Schema final : public userver::components::ComponentBase {
 public:
  static constexpr std::string_view kName = "schema";

  Schema(const userver::components::ComponentConfig& config,
         const userver::components::ComponentContext& context);
};

}  // namespace factoryctl::db
