#pragma once

// The crafting CPUs as the client sees them: the snapshot of the agent's
// last poll (db::saveCpus writes it).

#include <cstdint>
#include <optional>
#include <string>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <vector>

#include "db/stock.hpp"
#include "domain/order.hpp"

namespace factoryctl::db {

namespace pg = userver::storages::postgres;

struct CpuRow {
  std::string name;
  bool busy = false;
  // What it crafts; none when idle, or for goods no scan has seen yet.
  std::optional<StockRow> goods;
  std::int64_t remaining = 0;  // finalOutput().size: left to deliver
  domain::TimePoint seenAt;    // the poll that reported it
  // Our open order of those goods; none for a craft put on in game.
  std::optional<std::int64_t> order;
};

// By name; empty before the first poll.
auto listCpus(pg::Cluster& db) -> std::vector<CpuRow>;

}  // namespace factoryctl::db
