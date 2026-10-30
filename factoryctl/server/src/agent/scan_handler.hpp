#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::agent {

// POST /agent/v1/scan: a batch of the agent's scan (agent::parseScan). Answers
// a Lua table: {ok=true} or {ok=false, error="..."}, always with HTTP 200 —
// the agent looks at one field, not at status codes.
class ScanHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-agent-scan";

  ScanHandler(const userver::components::ComponentConfig& config,
              const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

// The {ok=...} answer as a Lua table.
auto replyLua(std::string_view error) -> std::string;

}  // namespace factoryctl::agent
