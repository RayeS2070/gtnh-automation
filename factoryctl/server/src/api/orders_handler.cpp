#include "api/orders_handler.hpp"

#include <algorithm>
#include <userver/components/component_context.hpp>
#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_status.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/utils/datetime.hpp>

#include "api/json.hpp"
#include "db/manual.hpp"
#include "db/schema.hpp"

namespace factoryctl::api {

namespace {

namespace json = userver::formats::json;
using userver::server::http::HttpStatus;

constexpr std::int64_t kDefaultLimit = 100;
constexpr std::int64_t kMaxLimit = 1000;

auto cluster(const userver::components::ComponentContext& context) {
  context.FindComponent<db::Schema>();
  return context.FindComponent<userver::components::Postgres>("postgres-db")
      .GetCluster();
}

auto fail(const userver::server::http::HttpRequest& request, HttpStatus status,
          std::string_view message) -> std::string {
  request.GetHttpResponse().SetStatus(status);
  return errorJson(message);
}

}  // namespace

OrdersHandler::OrdersHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto OrdersHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");

  if (request.GetMethod() == userver::server::http::HttpMethod::kGet) {
    const auto limit =
        parseId(request.GetArg("limit"))
            .transform([](auto n) { return std::min(n, kMaxLimit); })
            .value_or(kDefaultLimit);
    json::ValueBuilder items(userver::formats::common::Type::kArray);
    for (const auto& o : db::listOrders(*db_, limit)) items.PushBack(toJson(o));
    json::ValueBuilder body;
    body["items"] = std::move(items);
    return json::ToString(body.ExtractValue());
  }

  json::Value body;
  try {
    body = json::FromString(request.RequestBody());
  } catch (const json::Exception& e) {
    return fail(request, HttpStatus::kBadRequest,
                std::string("bad JSON: ") + e.what());
  }
  const auto order = parseOrderRequest(body);
  if (!order) return fail(request, HttpStatus::kBadRequest, order.error());
  const auto id =
      db::orderOnce(*db_, order->goodsId, order->amount, order->requestedBy,
                    userver::utils::datetime::Now());
  if (!id) return fail(request, HttpStatus::kBadRequest, id.error());
  LOG_INFO() << "order " << *id << ": " << order->amount << " of goods "
             << order->goodsId << " for '" << order->requestedBy << "'";
  json::ValueBuilder reply;
  reply["id"] = *id;
  return json::ToString(reply.ExtractValue());
}

OrderCancelHandler::OrderCancelHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto OrderCancelHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  const auto id = parseId(request.GetPathArg("order_id"));
  if (!id) return fail(request, HttpStatus::kBadRequest, "bad order id");
  const auto action =
      db::cancelOrder(*db_, *id, userver::utils::datetime::Now());
  if (!action) return fail(request, HttpStatus::kNotFound, action.error());
  const char* result = "nothing";
  switch (*action) {
    case domain::CancelAction::kCanceledNow:
      result = "canceled";
      break;
    case domain::CancelAction::kAskAgent:
      result = "asked";
      break;
    case domain::CancelAction::kNone:
      break;
  }
  LOG_INFO() << "order " << *id << " cancel: " << result;
  json::ValueBuilder reply;
  reply["result"] = result;
  return json::ToString(reply.ExtractValue());
}

}  // namespace factoryctl::api
