#include "db/events_cleanup.hpp"

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/logging/log.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/utils/datetime.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include "db/events.hpp"
#include "db/schema.hpp"

namespace factoryctl::db {

EventsCleanup::EventsCleanup(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : ComponentBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()),
      keep_(config["keep"].As<std::chrono::seconds>(std::chrono::days{30})) {
  context.FindComponent<Schema>();
  using userver::utils::PeriodicTask;
  task_.Start("events-cleanup",
              PeriodicTask::Settings{std::chrono::hours{1},
                                     {PeriodicTask::Flags::kNow}},
              [this] { run(); });
}

EventsCleanup::~EventsCleanup() { task_.Stop(); }

auto EventsCleanup::run() -> void {
  const auto gone =
      deleteEventsBefore(*db_, userver::utils::datetime::Now() - keep_);
  if (gone > 0) LOG_INFO() << "events: " << gone << " older ones deleted";
}

auto EventsCleanup::GetStaticConfigSchema() -> userver::yaml_config::Schema {
  return userver::yaml_config::MergeSchemas<ComponentBase>(R"(
type: object
description: deletes old events
additionalProperties: false
properties:
    keep: {type: string, description: how long events are kept (30d)}
)");
}

}  // namespace factoryctl::db
