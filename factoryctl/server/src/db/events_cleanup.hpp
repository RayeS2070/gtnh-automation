#pragma once

#include <chrono>
#include <string_view>
#include <userver/components/component_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <userver/utils/periodic_task.hpp>
#include <userver/yaml_config/schema.hpp>

namespace factoryctl::db {

// Deletes events older than `keep`, every hour and at start.
class EventsCleanup final : public userver::components::ComponentBase {
 public:
  static constexpr std::string_view kName = "events-cleanup";

  EventsCleanup(const userver::components::ComponentConfig& config,
                const userver::components::ComponentContext& context);
  ~EventsCleanup() override;

  static auto GetStaticConfigSchema() -> userver::yaml_config::Schema;

 private:
  auto run() -> void;

  userver::storages::postgres::ClusterPtr db_;
  std::chrono::seconds keep_;
  userver::utils::PeriodicTask task_;
};

}  // namespace factoryctl::db
