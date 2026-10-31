#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::api {

// GET /api/v1/cpus: the CPUs of the agent's last poll and the reserve low
// priority targets need, {"items": [...], "lowFreeCpus": 8}.
class CpusHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-cpus";

  CpusHandler(const userver::components::ComponentConfig& config,
              const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
  userver::dynamic_config::Source config_;
};

}  // namespace factoryctl::api
