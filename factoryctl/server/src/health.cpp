#include "health.hpp"

#include <userver/components/component_context.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/component.hpp>

namespace factoryctl {

Health::Health(const userver::components::ComponentConfig& config,
               const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()) {}

auto Health::HandleRequestThrow(const userver::server::http::HttpRequest&,
                                userver::server::request::RequestContext&) const
    -> std::string {
  namespace pg = userver::storages::postgres;
  const auto version =
      db_->Execute(pg::ClusterHostType::kMaster, "SELECT version()")
          .AsSingleRow<std::string>();

  userver::formats::json::ValueBuilder body;
  body["status"] = "ok";
  body["postgres"] = version;
  return userver::formats::json::ToString(body.ExtractValue());
}

}  // namespace factoryctl
