#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::agent {

// POST /agent/v1/bhc/poll: a black hole agent's report (parseBhcPoll) is
// stored with its sessions, the answer carries newer settings (replyBhcPoll).
// One poll is one transaction with the setup row locked. The agent polls only
// while its hole is closed, at its own pace: `poll` in the answer is advice.
class BhcPollHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-agent-bhc-poll";

  BhcPollHandler(const userver::components::ComponentConfig& config,
                 const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
};

}  // namespace factoryctl::agent
