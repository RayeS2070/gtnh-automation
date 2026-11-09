#include "api/bhc_handler.hpp"

#include <userver/components/component_context.hpp>
#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/http/parser/http_request_parse_args.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_status.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/utils/datetime.hpp>

#include "api/json.hpp"
#include "api/user.hpp"
#include "db/bhc.hpp"
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

// Path arguments come as they are in the URL: a browser sends bhc@-1,2,3 as
// bhc%40-1%2C2%2C3.
auto nameOf(const userver::server::http::HttpRequest& request) -> std::string {
  return userver::http::parser::UrlDecode(request.GetPathArg("name"));
}

auto utc(domain::TimePoint t) -> std::string {
  return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(t));
}

}  // namespace

BhcSetupsHandler::BhcSetupsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto BhcSetupsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  json::ValueBuilder items(userver::formats::common::Type::kArray);
  for (const auto& s : db::listBhcSetups(*db_)) {
    json::ValueBuilder e;
    e["name"] = s.name;
    if (s.coords) {
      e["coords"]["x"] = s.coords->x;
      e["coords"]["y"] = s.coords->y;
      e["coords"]["z"] = s.coords->z;
    } else {
      e["coords"] = json::ValueBuilder{};
    }
    e["seenAt"] = utc(s.seenAt);
    e["status"] = s.status;
    e["maxHolds"] = agent::kBhcMaxHolds;
    json::ValueBuilder sessions(userver::formats::common::Type::kArray);
    for (const auto& row : db::listBhcSessions(*db_, s.name, kSessionsShown)) {
      json::ValueBuilder x;
      x["session"] = row.s.session;
      x["closedAt"] = utc(row.closedAt);
      x["seconds"] = row.s.seconds;
      x["holds"] = row.s.holds;
      x["spent"] = row.s.spent;
      x["recipes"] = row.s.recipes;
      x["ending"] = row.s.ending;
      x["unknown"] = row.s.unknown;
      x["blocked"] = row.s.blocked;
      sessions.PushBack(std::move(x));
    }
    e["sessions"] = std::move(sessions);
    if (s.settings) {
      json::ValueBuilder settings(*s.settings);
      settings["version"] = s.settingsVersion;
      e["settings"] = std::move(settings);
    } else {
      e["settings"] = json::ValueBuilder{};
    }
    items.PushBack(std::move(e));
  }
  json::ValueBuilder body;
  body["items"] = std::move(items);
  return json::ToString(body.ExtractValue());
}

BhcSettingsHandler::BhcSettingsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto BhcSettingsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  const auto name = nameOf(request);
  json::Value body;
  try {
    body = json::FromString(request.RequestBody());
  } catch (const json::Exception& e) {
    return fail(request, HttpStatus::kBadRequest,
                std::string("bad JSON: ") + e.what());
  }
  const auto version = db::setBhcSettings(
      *db_, name, body, userver::utils::datetime::Now(), userOf(request));
  if (!version) return fail(request, HttpStatus::kBadRequest, version.error());
  if (!*version) return fail(request, HttpStatus::kNotFound, "no such setup");
  LOG_INFO() << "bhc " << name << ": settings v" << **version;
  json::ValueBuilder out;
  out["version"] = **version;
  return json::ToString(out.ExtractValue());
}

BhcSetupHandler::BhcSetupHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context), db_(cluster(context)) {}

auto BhcSetupHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  const auto name = nameOf(request);
  if (!db::deleteBhcSetup(*db_, name, userver::utils::datetime::Now(),
                          userOf(request))) {
    return fail(request, HttpStatus::kNotFound, "no such setup");
  }
  LOG_INFO() << "bhc " << name << " forgotten";
  return R"({"ok":true})";
}

}  // namespace factoryctl::api
