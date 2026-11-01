#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::agent {

// POST /agent/v1/pumps/poll: the pumping agent's report (parsePumpsPoll) is
// stored, the answer carries newer settings (replyPumpsPoll). One poll is one
// transaction with the settings row locked. The poll interval is the crafting
// agent's (FACTORYCTL_POLL poll-interval).
class PumpsPollHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-agent-pumps-poll";

  PumpsPollHandler(const userver::components::ComponentConfig& config,
                   const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
  userver::dynamic_config::Source config_;
};

}  // namespace factoryctl::agent
