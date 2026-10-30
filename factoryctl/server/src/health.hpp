#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl {

// GET /health: the server is up and reaches its database.
class Health final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-health";

  Health(const userver::components::ComponentConfig& config,
         const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

}  // namespace factoryctl
