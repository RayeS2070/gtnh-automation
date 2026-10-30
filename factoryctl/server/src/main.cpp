#include <userver/clients/dns/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/server/handlers/ping.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include "health.hpp"

auto main(int argc, char* argv[]) -> int {
  const auto components =
      userver::components::MinimalServerComponentList()
          .Append<userver::server::handlers::Ping>()
          // The Postgres component requires it, even without testsuite.
          .Append<userver::components::TestsuiteSupport>()
          .Append<userver::clients::dns::Component>()
          .Append<userver::components::Postgres>("postgres-db")
          .Append<factoryctl::Health>();
  return userver::utils::DaemonMain(argc, argv, components);
}
