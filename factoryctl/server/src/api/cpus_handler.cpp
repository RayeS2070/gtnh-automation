#include "api/cpus_handler.hpp"

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include "api/json.hpp"
#include "db/cpus.hpp"
#include "db/schema.hpp"
#include "domain/poll.hpp"

namespace factoryctl::api {

namespace json = userver::formats::json;

CpusHandler::CpusHandler(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()),
      lowFreeCpus_(config["low-free-cpus"].As<std::int32_t>(
          domain::Settings{}.lowFreeCpus)) {
  context.FindComponent<db::Schema>();
}

auto CpusHandler::GetStaticConfigSchema() -> userver::yaml_config::Schema {
  return userver::yaml_config::MergeSchemas<HttpHandlerBase>(R"(
type: object
description: the CPUs of the agent's last poll
additionalProperties: false
properties:
    low-free-cpus: {type: integer, description: shown with the CPUs; as for the poll}
)");
}

auto CpusHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  json::ValueBuilder items(userver::formats::common::Type::kArray);
  for (const auto& c : db::listCpus(*db_)) items.PushBack(toJson(c));
  json::ValueBuilder body;
  body["items"] = std::move(items);
  body["lowFreeCpus"] = lowFreeCpus_;
  return json::ToString(body.ExtractValue());
}

}  // namespace factoryctl::api
