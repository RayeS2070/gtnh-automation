#include "api/events_handler.hpp"

#include <algorithm>
#include <array>
#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/storages/postgres/component.hpp>

#include "api/json.hpp"
#include "db/events.hpp"
#include "db/schema.hpp"

namespace factoryctl::api {

namespace {

namespace json = userver::formats::json;

constexpr std::int64_t kDefaultLimit = 200;
constexpr std::int64_t kMaxLimit = 1000;
constexpr std::array<std::string_view, 4> kLevels{"debug", "info", "warning",
                                                  "error"};

auto queryOf(const userver::server::http::HttpRequest& request)
    -> db::EventQuery {
  db::EventQuery q;
  if (const auto& level = request.GetArg("level");
      std::ranges::contains(kLevels, level)) {
    q.minLevel = level;
  }
  q.source = request.GetArg("source");
  q.text = request.GetArg("q");
  q.order = parseId(request.GetArg("order"));
  q.before = parseId(request.GetArg("before"));
  q.limit = std::min(parseId(request.GetArg("limit")).value_or(kDefaultLimit),
                     kMaxLimit);
  return q;
}

}  // namespace

EventsHandler::EventsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()) {
  context.FindComponent<db::Schema>();
}

auto EventsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  json::ValueBuilder items(userver::formats::common::Type::kArray);
  for (const auto& e : db::listEvents(*db_, queryOf(request))) {
    items.PushBack(toJson(e));
  }
  json::ValueBuilder body;
  body["items"] = std::move(items);
  return json::ToString(body.ExtractValue());
}

}  // namespace factoryctl::api
