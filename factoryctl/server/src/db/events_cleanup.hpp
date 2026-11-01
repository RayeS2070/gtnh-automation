#pragma once

#include <string_view>
#include <userver/components/component_base.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <userver/utils/periodic_task.hpp>

namespace factoryctl::db {

// Deletes events and pump cycles older than config::kEvents keep, every hour
// and at start.
class EventsCleanup final : public userver::components::ComponentBase {
 public:
  static constexpr std::string_view kName = "events-cleanup";

  EventsCleanup(const userver::components::ComponentConfig& config,
                const userver::components::ComponentContext& context);
  ~EventsCleanup() override;

 private:
  auto run() -> void;

  userver::storages::postgres::ClusterPtr db_;
  userver::dynamic_config::Source config_;
  userver::utils::PeriodicTask task_;
};

}  // namespace factoryctl::db
