#include <userver/clients/dns/component.hpp>
#include <userver/components/fs_cache.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/server/handlers/http_handler_static.hpp>
#include <userver/server/handlers/ping.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include "agent/poll_handler.hpp"
#include "agent/scan_handler.hpp"
#include "api/cpus_handler.hpp"
#include "api/events_handler.hpp"
#include "api/orders_handler.hpp"
#include "api/stock_handler.hpp"
#include "api/targets_handler.hpp"
#include "config/file_updater.hpp"
#include "db/events_cleanup.hpp"
#include "db/schema.hpp"
#include "health.hpp"
#include "http/compression.hpp"

auto main(int argc, char* argv[]) -> int {
  const auto components =
      userver::components::MinimalServerComponentList()
          .Append<userver::server::handlers::Ping>()
          .Append<factoryctl::config::FileUpdater>()
          .Append<factoryctl::http::CompressionFactory>()
          // The Postgres component requires it, even without testsuite.
          .Append<userver::components::TestsuiteSupport>()
          .Append<userver::clients::dns::Component>()
          .Append<userver::components::Postgres>("postgres-db")
          .Append<factoryctl::db::Schema>()
          .Append<factoryctl::db::EventsCleanup>()
          .Append<factoryctl::Health>()
          .Append<factoryctl::agent::ScanHandler>()
          .Append<factoryctl::agent::PollHandler>()
          .Append<factoryctl::api::StockHandler>()
          .Append<factoryctl::api::TargetsHandler>()
          .Append<factoryctl::api::TargetHandler>()
          .Append<factoryctl::api::TargetGroupsHandler>()
          .Append<factoryctl::api::TargetGroupHandler>()
          .Append<factoryctl::api::OrdersHandler>()
          .Append<factoryctl::api::OrderCancelHandler>()
          .Append<factoryctl::api::CpusHandler>()
          .Append<factoryctl::api::EventsHandler>()
          // The web client (factoryctl/web, built): everything not matched
          // above.
          .Append<userver::components::FsCache>("fs-cache-web")
          .Append<userver::server::handlers::HttpHandlerStatic>()
          // Item icons: the GTNH Calculator's export (assets/data) at /data/.
          .Append<userver::components::FsCache>("fs-cache-data")
          .Append<userver::server::handlers::HttpHandlerStatic>(
              "handler-static-data");
  return userver::utils::DaemonMain(argc, argv, components);
}
