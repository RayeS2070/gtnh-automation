#include "api/stock_handler.hpp"

#include <charconv>
#include <optional>
#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/storages/postgres/component.hpp>

#include "api/json.hpp"
#include "db/schema.hpp"
#include "db/stock.hpp"

namespace factoryctl::api {

namespace {

namespace json = userver::formats::json;

// No limit or an unusable one: everything (the whole network is some 5000
// rows, the page shows them all).
auto limitOf(const std::string& arg) -> std::optional<std::int64_t> {
  std::int64_t n = 0;
  const auto [end, ec] =
      std::from_chars(arg.data(), arg.data() + arg.size(), n);
  if (arg.empty() || ec != std::errc{} || end != arg.data() + arg.size() ||
      n <= 0) {
    return std::nullopt;
  }
  return n;
}

auto kindOf(std::string_view kind) -> db::GoodsKind {
  if (kind == "items") return db::GoodsKind::kItems;
  if (kind == "fluids") return db::GoodsKind::kFluids;
  return db::GoodsKind::kAll;
}

// Unknown values mean the default, as an unusable limit does.
auto queryOf(const userver::server::http::HttpRequest& request)
    -> db::StockQuery {
  const auto& kind = request.GetArg("kind");
  return {
      .text = request.GetArg("q"),
      .kind = kindOf(kind),
      .order = request.GetArg("sort") == "amount" ? db::StockOrder::kAmount
                                                  : db::StockOrder::kLabel,
      .descending = request.GetArg("dir") == "desc",
      .limit = limitOf(request.GetArg("limit")),
  };
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
  const auto rows = db::findStock(*db_, queryOf(request));
  json::ValueBuilder items(userver::formats::common::Type::kArray);
  for (const auto& r : rows) items.PushBack(toJson(r));
  json::ValueBuilder body;
  body["items"] = std::move(items);
  return json::ToString(body.ExtractValue());
}

}  // namespace factoryctl::api
