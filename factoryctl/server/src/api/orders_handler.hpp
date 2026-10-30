#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::api {

// GET /api/v1/orders?limit=<n>: {"items": [orders, newest first]}
// POST /api/v1/orders {"goodsId", "amount", "requestedBy"}: a one-off order,
//   queued for the agent's next poll: 200 {"id": 7}; 400 {"error": "..."}.
class OrdersHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-orders";

  OrdersHandler(const userver::components::ComponentConfig& config,
                const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

// POST /api/v1/orders/{order_id}/cancel: {"result": "canceled" | "asked" |
// "nothing"} — canceled at once (it was queued), asked of the agent (it
// cancels the craft on its CPU), or nothing to do (closed, or asked before).
class OrderCancelHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-order-cancel";

  OrderCancelHandler(const userver::components::ComponentConfig& config,
                     const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

}  // namespace factoryctl::api
