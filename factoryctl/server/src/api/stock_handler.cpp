#include "api/stock_handler.hpp"

#include <algorithm>
#include <charconv>
#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/storages/postgres/component.hpp>

#include "db/schema.hpp"
#include "db/stock.hpp"

namespace factoryctl::api {

namespace {

namespace json = userver::formats::json;

constexpr std::int64_t kDefaultLimit = 200;
constexpr std::int64_t kMaxLimit = 2000;

auto limitOf(const std::string& arg) -> std::int64_t {
  std::int64_t n = 0;
  const auto [end, ec] =
      std::from_chars(arg.data(), arg.data() + arg.size(), n);
  if (arg.empty() || ec != std::errc{} || end != arg.data() + arg.size() ||
      n <= 0) {
    return kDefaultLimit;
  }
  return std::min(n, kMaxLimit);
}

}  // namespace

StockHandler::StockHandler(const userver::components::ComponentConfig& config,
                           const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()) {
  context.FindComponent<db::Schema>();
}

auto StockHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  const auto rows = db::findStock(*db_, request.GetArg("q"),
                                  limitOf(request.GetArg("limit")));
  json::ValueBuilder items(userver::formats::common::Type::kArray);
  for (const auto& r : rows) {
    json::ValueBuilder e;
    e["goodsId"] = r.goodsId;
    e["name"] = r.key.name;
    e["damage"] = r.key.damage;
    e["variant"] = r.key.variant;
    e["label"] = r.label;
    e["amount"] = r.amount;
    items.PushBack(std::move(e));
  }
  json::ValueBuilder body;
  body["items"] = std::move(items);
  return json::ToString(body.ExtractValue());
}

}  // namespace factoryctl::api
