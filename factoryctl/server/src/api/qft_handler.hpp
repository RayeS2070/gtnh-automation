#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::api {

// GET /api/v1/qft: {"items": [{"name", "coords": {"x", "y", "z"} or null,
//   "seenAt", "catalog", "status", "settings": {"version", "mode", "recipes"}
//   or null}]} — catalog and status as the agent sent them
//   (agent::QftReport).
class QftSetupsHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-qft";

  QftSetupsHandler(const userver::components::ComponentConfig& config,
                   const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

// PUT /api/v1/qft/{name}/settings {"mode", "recipes"}: 200 {"version"};
//   checked against the setup's catalog (agent::checkQftSettings), 400 with
//   the reason, 404 for an unknown setup. The agent takes them with its next
//   poll.
class QftSettingsHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-qft-settings";

  QftSettingsHandler(const userver::components::ComponentConfig& config,
                     const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

// DELETE /api/v1/qft/{name}: forgets the setup (its settings too): 200
//   {"ok": true}, 404 when unknown. A QFT agent still running under the name
//   comes back as a new setup and offers its config.
class QftSetupHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-qft-setup";

  QftSetupHandler(const userver::components::ComponentConfig& config,
                  const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

}  // namespace factoryctl::api
