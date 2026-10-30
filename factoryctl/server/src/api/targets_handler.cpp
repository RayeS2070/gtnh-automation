#include "api/targets_handler.hpp"

#include <userver/components/component_context.hpp>
#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/http/common_headers.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_status.hpp>
#include <userver/storages/postgres/component.hpp>

#include "api/json.hpp"
#include "db/schema.hpp"
#include "db/targets.hpp"

namespace factoryctl::api {

namespace {

namespace json = userver::formats::json;
using userver::server::http::HttpStatus;

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

TargetsHandler::TargetsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto TargetsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  json::ValueBuilder items(userver::formats::common::Type::kArray);
  for (const auto& t : db::listTargets(*db_)) items.PushBack(toJson(t));
  json::ValueBuilder body;
  body["items"] = std::move(items);
  return json::ToString(body.ExtractValue());
}

TargetHandler::TargetHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto TargetHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  const auto id = parseId(request.GetPathArg("goods_id"));
  if (!id) return fail(request, HttpStatus::kBadRequest, "bad goods id");

  if (request.GetMethod() == userver::server::http::HttpMethod::kDelete) {
    if (!db::deleteTarget(*db_, *id)) {
      return fail(request, HttpStatus::kNotFound, "no target");
    }
    LOG_INFO() << "target of goods " << *id << " deleted";
    return R"({"ok":true})";
  }

  json::Value body;
  try {
    body = json::FromString(request.RequestBody());
  } catch (const json::Exception& e) {
    return fail(request, HttpStatus::kBadRequest,
                std::string("bad JSON: ") + e.what());
  }
  const auto settings = parseTargetSettings(body);
  if (!settings) {
    return fail(request, HttpStatus::kBadRequest, settings.error());
  }
  if (auto ok = db::setTarget(*db_, *id, *settings); !ok) {
    // Bad numbers or unknown goods; both are the caller's to fix.
    return fail(request, HttpStatus::kBadRequest, ok.error());
  }
  LOG_INFO() << "target of goods " << *id << " set: threshold "
             << settings->threshold << ", batch " << settings->batch;
  return R"({"ok":true})";
}

}  // namespace factoryctl::api
