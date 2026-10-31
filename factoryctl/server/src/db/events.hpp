#pragma once

// Events: what happened, for people (the Logs tab, an order's history). Not
// the server's log: only what our code says on purpose.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <vector>

#include "domain/order.hpp"
#include "domain/plan.hpp"

namespace factoryctl::db {

namespace pg = userver::storages::postgres;

struct Event {
  domain::TimePoint at;
  std::string level = "info";  // debug | info | warning | error
  std::string source;          // agent | server | order | scan | target
  std::string text;
  std::optional<std::int64_t> order;
  // The goods: by id, else by key, else the order's. Unknown goods: none.
  std::optional<std::int64_t> goodsId;
  std::optional<domain::ItemKey> goods;
  std::optional<double> agentUptime;  // the agent's own lines
};

// In the transaction of what they tell: a step done again after a failure
// does not tell it twice.
auto addEvents(pg::Transaction& tx, std::span<const Event> events) -> void;

struct GoodsRef {
  std::int64_t id = 0;
  domain::ItemKey key;
  std::string label;
};

struct EventRow {
  std::int64_t id = 0;
  domain::TimePoint at;
  std::string level;
  std::string source;
  std::string text;
  std::optional<std::int64_t> order;
  std::optional<GoodsRef> goods;
  std::optional<double> agentUptime;
};

struct EventQuery {
  std::string minLevel = "debug";  // this level and above
  std::string source;              // empty: any
  std::string text;                // part of the text, any case; empty: any
  std::optional<std::int64_t> order;
  std::optional<std::int64_t> before;  // ids below it: the next page
  std::int64_t limit = 200;
};

// Newest first.
auto listEvents(pg::Cluster& db, const EventQuery& query)
    -> std::vector<EventRow>;

// Returns how many went.
auto deleteEventsBefore(pg::Cluster& db, domain::TimePoint before)
    -> std::int64_t;

}  // namespace factoryctl::db
