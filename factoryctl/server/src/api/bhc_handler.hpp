#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::api {

// GET /api/v1/bhc: {"items": [{"name", "coords": {"x", "y", "z"} or null,
//   "seenAt", "status": {"state", "supplies"}, "settings": {"version",
//   "holdAt", "floor", "maxHolds", "minSpacetime"} or null, "maxHolds": the
//   most allowed, "sessions": [{"session", "closedAt", "seconds", "holds",
//   "spent", "recipes", "ending", "unknown", "blocked"}]}]} — status as the
//   agent sent it (agent::BhcReport), the newest kSessionsShown sessions.
class BhcSetupsHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::int64_t kSessionsShown = 30;

  static constexpr std::string_view kName = "handler-api-bhc";

  BhcSetupsHandler(const userver::components::ComponentConfig& config,
                   const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

// PUT /api/v1/bhc/{name}/settings {"holdAt", "floor", "maxHolds",
//   "minSpacetime"}: 200 {"version"}; checked (agent::checkBhcSettings), 400
//   with the reason, 404 for an unknown setup. The agent takes them with its
//   next poll, made only while its hole is closed.
class BhcSettingsHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-bhc-settings";

  BhcSettingsHandler(const userver::components::ComponentConfig& config,
                     const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

// DELETE /api/v1/bhc/{name}: forgets the setup (its settings and sessions
//   too): 200 {"ok": true}, 404 when unknown. An agent still running under the
//   name comes back as a new setup and offers its config.
class BhcSetupHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-bhc-setup";

  BhcSetupHandler(const userver::components::ComponentConfig& config,
                  const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

}  // namespace factoryctl::api
