#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::api {

// GET /api/v1/mining: {"setups": [{"name", "run", "seenAt", "status", "plan"}],
//   "targets": [goods fields + "threshold", "priority", "enabled",
//               "ores": [{"name", "damage", "label"}]],
//   "plasma": {"tier", "label"} or null, "bufferLimit"}.
class MiningHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-mining";

  MiningHandler(const userver::components::ComponentConfig& config,
                const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
  userver::dynamic_config::Source config_;
};

// PUT /api/v1/mining/targets/{goods_id} {"threshold", "priority", "enabled",
//   "ores": [{"name", "damage"}]}: 200 {"ok"}; DELETE: 200, 404 when none.
// The agents get the change with their next poll. Bad input: 400 {"error"}.
class MiningTargetHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-mining-target";

  MiningTargetHandler(const userver::components::ComponentConfig& config,
                      const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

// GET /api/v1/mining/sources/{goods_id}: the module outputs that are the goods
// or that processing turns into it, the likeliest first (fewer hops):
// [{"name", "damage", "label", "hops", "asteroids": [names]}].
// DELETE /api/v1/mining/setups/{name}: forget a setup (404 when unknown).
class MiningSourcesHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-mining-sources";

  MiningSourcesHandler(const userver::components::ComponentConfig& config,
                       const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

class MiningSetupHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-mining-setup";

  MiningSetupHandler(const userver::components::ComponentConfig& config,
                     const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

}  // namespace factoryctl::api
