#pragma once

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <userver/yaml_config/fwd.hpp>

#include "domain/poll.hpp"

namespace factoryctl::agent {

// POST /agent/v1/poll: the agent's report (agent::parsePoll) moves the
// orders on, the answer hands out work (agent::replyPoll). One poll is one
// transaction with the open orders locked. Periods and scan parameters come
// from the static config: short ones make debugging faster.
class PollHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-agent-poll";

  PollHandler(const userver::components::ComponentConfig& config,
              const userver::components::ComponentContext& context);

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;

  static auto GetStaticConfigSchema() -> userver::yaml_config::Schema;

 private:
  userver::storages::postgres::ClusterPtr db_;
  domain::Settings settings_;
};

}  // namespace factoryctl::agent
