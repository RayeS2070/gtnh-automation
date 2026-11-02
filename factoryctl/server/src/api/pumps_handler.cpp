#include "api/pumps_handler.hpp"

#include <chrono>
#include <userver/components/component_context.hpp>
#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/http/parser/http_request_parse_args.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_status.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/utils/datetime.hpp>

#include "api/json.hpp"
#include "api/user.hpp"
#include "db/pumps.hpp"
#include "db/schema.hpp"

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

auto versionJson(std::int64_t version) -> std::string {
  json::ValueBuilder b;
  b["version"] = version;
  return json::ToString(b.ExtractValue());
}

}  // namespace

PumpsHandler::PumpsHandler(const userver::components::ComponentConfig& config,
                           const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto PumpsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  const auto since = userver::utils::datetime::Now() - std::chrono::hours{24};
  return json::ToString(toJson(db::pumpsView(*db_, since)).ExtractValue());
}

PumpSettingsHandler::PumpSettingsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto PumpSettingsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  const auto body = parseBody(request);
  if (!body) return fail(request, HttpStatus::kBadRequest, body.error());
  const auto general = parsePumpGeneral(*body);
  if (!general) return fail(request, HttpStatus::kBadRequest, general.error());
  const auto version =
      db::setPumpGeneral(*db_, general->batch, general->threshold,
                         userver::utils::datetime::Now(), userOf(request));
  if (!version) return fail(request, HttpStatus::kBadRequest, version.error());
  LOG_INFO() << "pumping settings v" << *version << ": batch " << general->batch
             << ", threshold " << general->threshold;
  return versionJson(*version);
}

PumpTargetHandler::PumpTargetHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto PumpTargetHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  // As in the URL: decode (a fluid name with an odd character would come %-encoded).
  const auto fluid = userver::http::parser::UrlDecode(request.GetPathArg("fluid"));
  if (fluid.empty()) return fail(request, HttpStatus::kBadRequest, "no fluid");
  const auto now = userver::utils::datetime::Now();

  if (request.GetMethod() == userver::server::http::HttpMethod::kDelete) {
    const auto version =
        db::deletePumpTarget(*db_, fluid, now, userOf(request));
    if (!version) return fail(request, HttpStatus::kNotFound, "no target");
    LOG_INFO() << "pumping target " << fluid << " deleted, v" << *version;
    return versionJson(*version);
  }
  const auto body = parseBody(request);
  if (!body) return fail(request, HttpStatus::kBadRequest, body.error());
  const auto target = parsePumpTarget(*body, fluid);
  if (!target) return fail(request, HttpStatus::kBadRequest, target.error());
  const auto version = db::setPumpTarget(*db_, *target, now, userOf(request));
  if (!version) return fail(request, HttpStatus::kBadRequest, version.error());
  LOG_INFO() << "pumping target " << fluid << ": " << db::describe(*target)
             << ", v" << *version;
  return versionJson(*version);
}

}  // namespace factoryctl::api
