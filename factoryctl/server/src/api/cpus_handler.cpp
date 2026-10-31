#include "api/cpus_handler.hpp"

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/storages/postgres/component.hpp>

#include "api/json.hpp"
#include "config/dynamic.hpp"
#include "db/cpus.hpp"
#include "db/schema.hpp"

namespace factoryctl::api {

namespace json = userver::formats::json;

CpusHandler::CpusHandler(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()),
      config_(context.FindComponent<userver::components::DynamicConfig>()
                  .GetSource()) {
  context.FindComponent<db::Schema>();
}

auto CpusHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  json::ValueBuilder items(userver::formats::common::Type::kArray);
  for (const auto& c : db::listCpus(*db_)) items.PushBack(toJson(c));
  json::ValueBuilder body;
  body["items"] = std::move(items);
  body["lowFreeCpus"] = config_.GetCopy(config::kPoll).lowFreeCpus;
  return json::ToString(body.ExtractValue());
}

}  // namespace factoryctl::api
