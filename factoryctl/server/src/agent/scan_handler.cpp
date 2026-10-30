#include "agent/scan_handler.hpp"

#include <userver/components/component_context.hpp>
#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/utils/datetime.hpp>

#include "agent/lua.hpp"
#include "agent/protocol.hpp"
#include "db/scan.hpp"
#include "db/schema.hpp"
#include "domain/scan.hpp"

namespace factoryctl::agent {

namespace json = userver::formats::json;

auto replyLua(std::string_view error) -> std::string {
  json::ValueBuilder b;
  b["ok"] = error.empty();
  if (!error.empty()) b["error"] = std::string(error);
  // Only a bool and a string: nothing toLua could refuse.
  return toLua(b.ExtractValue()).value_or("{ok=false}");
}

ScanHandler::ScanHandler(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      db_(context.FindComponent<userver::components::Postgres>("postgres-db")
              .GetCluster()) {
  // Start after the schema is in place.
  context.FindComponent<db::Schema>();
}

auto ScanHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("text/plain; charset=utf-8");

  json::Value body;
  try {
    body = json::FromString(request.RequestBody());
  } catch (const json::Exception& e) {
    LOG_WARNING() << "scan: bad JSON: " << e.what();
    return replyLua(std::string("bad JSON: ") + e.what());
  }
  const auto batch = parseScan(body);
  if (!batch) {
    LOG_WARNING() << "scan: " << batch.error();
    return replyLua(batch.error());
  }

  const auto goods = domain::mergeBatch(batch->stacks);
  if (auto ok = db::storeBatch(*db_, batch->scan, goods); !ok) {
    LOG_WARNING() << "scan: " << ok.error();
    return replyLua(ok.error());
  }
  if (batch->last) {
    if (auto ok =
            db::finishScan(*db_, batch->scan, userver::utils::datetime::Now());
        !ok) {
      LOG_WARNING() << "scan: " << ok.error();
      return replyLua(ok.error());
    }
    LOG_INFO() << "scan " << batch->scan << " finished";
  }
  return replyLua("");
}

}  // namespace factoryctl::agent
