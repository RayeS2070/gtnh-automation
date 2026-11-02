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

// POST /api/v1/mining/targets {"product": {"name", "damage", "variant",
//   "label"}, "threshold", "priority", "enabled", "ores"}: a target on any item,
//   in the stock or not (a product below the scan threshold): 200 {"goodsId"}.
class MiningTargetsHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-mining-targets";

  MiningTargetsHandler(const userver::components::ComponentConfig& config,
                       const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

// GET /api/v1/mining/ores: every module output (an ore, a dust), by label:
// [{"name", "damage", "label", "asteroids": [names],
//   "products": [{"name", "damage"}]}] - what processing makes of it
// (macerating, washing, centrifuging...; chemistry is not followed).
class MiningOresHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-mining-ores";

  using HttpHandlerBase::HttpHandlerBase;

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;
};

// DELETE /api/v1/mining/setups/{name}: forget a setup (404 when unknown).
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
