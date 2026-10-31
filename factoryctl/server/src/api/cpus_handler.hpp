#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <userver/yaml_config/schema.hpp>

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

  static auto GetStaticConfigSchema() -> userver::yaml_config::Schema;

 private:
  userver::storages::postgres::ClusterPtr db_;
  std::int32_t lowFreeCpus_;
};

}  // namespace factoryctl::api
