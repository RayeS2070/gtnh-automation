#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>

namespace factoryctl::agent {

// POST /agent/v1/mining/poll: a mining agent's report (parseMiningPoll) is
// stored and its modules planned (domain::planMining) against the targets, the
// stock, the other setups' plans; the answer is the plan (replyMiningPoll).
// One poll is one transaction with the setup row locked. The poll interval is
// the crafting agent's (FACTORYCTL_POLL poll-interval).
class MiningPollHandler final
    : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-agent-mining-poll";

  MiningPollHandler(const userver::components::ComponentConfig& config,
                   const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

 private:
  userver::storages::postgres::ClusterPtr db_;
  userver::dynamic_config::Source config_;
};

}  // namespace factoryctl::agent
