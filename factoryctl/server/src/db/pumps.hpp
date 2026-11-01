#pragma once

// The pumping agent's data: its settings (edited in the client, taken by the
// agent), what it reports (modules, cycles, the pumping network's stock).

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <vector>

#include "db/events.hpp"
#include "domain/order.hpp"
#include "domain/pumps.hpp"

namespace factoryctl::db {

namespace pg = userver::storages::postgres;

// --- the agent's poll (in its transaction) ----------------------------------

// With the targets; the row locked. None: the agent's offer is welcome.
auto loadPumpSettings(pg::Transaction& tx)
    -> std::optional<domain::PumpSettings>;

// Replaces the settings and every target, at s.version.
auto storePumpSettings(pg::Transaction& tx, const domain::PumpSettings& s,
                       domain::TimePoint now) -> void;

// Recipes, modules (replaced), cycles (once each), stock. Returns what people
// should hear of: modules that came or went. Failed cycles are the agent's
// own log lines, not told twice.
auto storePumpsReport(pg::Transaction& tx, const domain::PumpReport& r,
                      domain::TimePoint now) -> std::vector<Event>;

// --- the client --------------------------------------------------------------

struct PumpModuleRow {
  domain::PumpModule module;
  domain::TimePoint seenAt;
};

// Every fluid the modules can pump, with its target if it has one.
struct PumpFluidRow {
  domain::PumpRecipe recipe;
  std::optional<domain::PumpTarget> target;
  std::optional<std::int64_t> amount;  // the pumping network, last report
  std::optional<std::int64_t> goodsId;  // the same fluid in the stock: icon
  std::int64_t inFlight = 0;  // what running cycles bring
  std::int64_t produced = 0;  // by done cycles since `since`
};

struct PumpsView {
  std::optional<domain::PumpSettings> settings;  // targets left empty
  std::vector<PumpModuleRow> modules;
  std::vector<PumpFluidRow> fluids;
  std::int64_t failed = 0;  // cycles since `since`
};

auto pumpsView(pg::Cluster& db, domain::TimePoint since) -> PumpsView;

// The batch and the threshold; the version goes up. Fails for bad values.
auto setPumpGeneral(pg::Cluster& db, std::int32_t batch, double threshold,
                    domain::TimePoint now)
    -> std::expected<std::int64_t, std::string>;

// Creates or replaces a target; the version goes up. Fails for a fluid no
// module can pump and for bad numbers.
auto setPumpTarget(pg::Cluster& db, const domain::PumpTarget& t,
                   domain::TimePoint now)
    -> std::expected<std::int64_t, std::string>;

// The new version; none when there was no such target.
auto deletePumpTarget(pg::Cluster& db, const std::string& fluid,
                      domain::TimePoint now) -> std::optional<std::int64_t>;

auto deletePumpCyclesBefore(pg::Cluster& db, domain::TimePoint before)
    -> std::int64_t;

// "keep 10000000000, priority 2, paused": for events.
auto describe(const domain::PumpTarget& t) -> std::string;

}  // namespace factoryctl::db
