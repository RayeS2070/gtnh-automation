#include "db/events_cleanup.hpp"

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/logging/log.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/utils/datetime.hpp>

#include "config/dynamic.hpp"
#include "db/events.hpp"
#include "db/pumps.hpp"
#include "db/schema.hpp"

namespace factoryctl::db {

EventsCleanup::EventsCleanup(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : ComponentBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()),
      config_(context.FindComponent<userver::components::DynamicConfig>()
                  .GetSource()) {
  context.FindComponent<Schema>();
  using userver::utils::PeriodicTask;
  task_.Start("events-cleanup",
              PeriodicTask::Settings{std::chrono::hours{1},
                                     {PeriodicTask::Flags::kNow}},
              [this] { run(); });
}

EventsCleanup::~EventsCleanup() { task_.Stop(); }

auto EventsCleanup::run() -> void {
  const auto keep = config_.GetCopy(config::kEvents).keep;
  const auto before = userver::utils::datetime::Now() - keep;
  const auto gone = deleteEventsBefore(*db_, before);
  if (gone > 0) LOG_INFO() << "events: " << gone << " older ones deleted";
  // Pump cycles are history the same way: kept as long.
  const auto cycles = deletePumpCyclesBefore(*db_, before);
  if (cycles > 0) LOG_INFO() << "pump cycles: " << cycles << " older ones deleted";
}

}  // namespace factoryctl::db
