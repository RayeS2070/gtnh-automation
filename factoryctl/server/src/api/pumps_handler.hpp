#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::api {

// GET /api/v1/pumps: settings, modules, fluids (api::toJson(PumpsView));
// "produced" and "failed" count the last 24 hours.
// PUT /api/v1/pumps/settings {"batch", "threshold"}: 200 {"version"}.
// PUT /api/v1/pumps/targets/{fluid} {"target", "priority", "enabled"}:
//   200 {"version"}; DELETE: 200 {"version"}, 404 when there was none.
// A change raises the settings' version: the agent takes it with its next
// poll. Errors of the input: 400 {"error"}.
class PumpsHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-pumps";

  PumpsHandler(const userver::components::ComponentConfig& config,
               const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

class PumpSettingsHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-pump-settings";

  PumpSettingsHandler(const userver::components::ComponentConfig& config,
                      const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

class PumpTargetHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-pump-target";

  PumpTargetHandler(const userver::components::ComponentConfig& config,
                    const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

}  // namespace factoryctl::api
