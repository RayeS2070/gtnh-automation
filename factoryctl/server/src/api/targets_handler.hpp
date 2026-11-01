#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::api {

// GET /api/v1/targets: {"items": [goods fields + target settings]}
class TargetsHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-targets";

  TargetsHandler(const userver::components::ComponentConfig& config,
                 const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

// PUT /api/v1/targets/{goods_id} with the settings (api::parseTargetSettings):
//   200 {"ok": true}, 400/404 {"error": "..."}.
// DELETE /api/v1/targets/{goods_id}: 200 {"ok": true}, 404 when there was none.
class TargetHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-target";

  TargetHandler(const userver::components::ComponentConfig& config,
                const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

// GET /api/v1/target-groups: {"items": [{"id", "name", "targets"}]}
// POST /api/v1/target-groups {"name"}: 200 {"id"}, 400 {"error"}.
class TargetGroupsHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-target-groups";

  TargetGroupsHandler(const userver::components::ComponentConfig& config,
                      const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

// PUT /api/v1/target-groups/{group_id} {"name"}: renames it.
// DELETE /api/v1/target-groups/{group_id}: its targets stay, ungrouped.
// 200 {"ok": true}, 400/404 {"error"}.
class TargetGroupHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-target-group";

  TargetGroupHandler(const userver::components::ComponentConfig& config,
                     const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

}  // namespace factoryctl::api
