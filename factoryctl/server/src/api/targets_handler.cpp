#include "api/targets_handler.hpp"

#include <userver/components/component_context.hpp>
#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/http/common_headers.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_status.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/utils/datetime.hpp>

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

auto parseBody(const userver::server::http::HttpRequest& request)
    -> std::expected<json::Value, std::string> {
  try {
    return json::FromString(request.RequestBody());
  } catch (const json::Exception& e) {
    return std::unexpected(std::string("bad JSON: ") + e.what());
  }
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
    if (!db::deleteTarget(*db_, *id, userver::utils::datetime::Now())) {
      return fail(request, HttpStatus::kNotFound, "no target");
    }
    LOG_INFO() << "target of goods " << *id << " deleted";
    return R"({"ok":true})";
  }

  const auto body = parseBody(request);
  if (!body) return fail(request, HttpStatus::kBadRequest, body.error());
  const auto settings = parseTargetSettings(*body);
  if (!settings) {
    return fail(request, HttpStatus::kBadRequest, settings.error());
  }
  if (auto ok = db::setTarget(*db_, *id, *settings,
                                 userver::utils::datetime::Now()); !ok) {
    // Bad numbers or unknown goods; both are the caller's to fix.
    return fail(request, HttpStatus::kBadRequest, ok.error());
  }
  LOG_INFO() << "target of goods " << *id << " set: threshold "
             << settings->threshold << ", batch " << settings->batch;
  return R"({"ok":true})";
}

TargetGroupsHandler::TargetGroupsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto TargetGroupsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  if (request.GetMethod() == userver::server::http::HttpMethod::kGet) {
    json::ValueBuilder items(userver::formats::common::Type::kArray);
    for (const auto& g : db::listGroups(*db_)) items.PushBack(toJson(g));
    json::ValueBuilder out;
    out["items"] = std::move(items);
    return json::ToString(out.ExtractValue());
  }
  const auto body = parseBody(request);
  if (!body) return fail(request, HttpStatus::kBadRequest, body.error());
  const auto name = parseGroupName(*body);
  if (!name) return fail(request, HttpStatus::kBadRequest, name.error());
  const auto id = db::addGroup(*db_, *name, userver::utils::datetime::Now());
  if (!id) return fail(request, HttpStatus::kBadRequest, id.error());
  LOG_INFO() << "target group " << *id << " added: " << *name;
  json::ValueBuilder out;
  out["id"] = *id;
  return json::ToString(out.ExtractValue());
}

TargetGroupHandler::TargetGroupHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto TargetGroupHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  const auto id = parseId(request.GetPathArg("group_id"));
  if (!id) return fail(request, HttpStatus::kBadRequest, "bad group id");
  const auto now = userver::utils::datetime::Now();

  if (request.GetMethod() == userver::server::http::HttpMethod::kDelete) {
    if (!db::deleteGroup(*db_, *id, now)) {
      return fail(request, HttpStatus::kNotFound, "no group");
    }
    LOG_INFO() << "target group " << *id << " deleted";
    return R"({"ok":true})";
  }
  const auto body = parseBody(request);
  if (!body) return fail(request, HttpStatus::kBadRequest, body.error());
  const auto name = parseGroupName(*body);
  if (!name) return fail(request, HttpStatus::kBadRequest, name.error());
  const auto renamed = db::renameGroup(*db_, *id, *name, now);
  if (!renamed) return fail(request, HttpStatus::kBadRequest, renamed.error());
  if (!*renamed) return fail(request, HttpStatus::kNotFound, "no group");
  LOG_INFO() << "target group " << *id << " renamed: " << *name;
  return R"({"ok":true})";
}

}  // namespace factoryctl::api
