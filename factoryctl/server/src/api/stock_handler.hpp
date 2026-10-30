#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::api {

// GET /api/v1/stock?q=<label part>&limit=<n>:
//   {"items": [{"goodsId", "name", "damage", "variant", "label", "amount"}]}
// Amounts stay below 2^53 (OC hands out at most 2^52 - 1): exact in JS.
class StockHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-stock";

  StockHandler(const userver::components::ComponentConfig& config,
               const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

}  // namespace factoryctl::api
