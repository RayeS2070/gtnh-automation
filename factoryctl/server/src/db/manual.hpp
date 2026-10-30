#pragma once

// Orders as the client sees and makes them: one-off (manual) orders,
// the order list, cancel.

#include <cstdint>
#include <expected>
#include <string>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <vector>

#include "db/stock.hpp"
#include "domain/order.hpp"

namespace factoryctl::db {

namespace pg = userver::storages::postgres;

struct OrderRow {
  std::int64_t id = 0;
  StockRow goods;
  std::int64_t amount = 0;
  std::string status;  // as in the database: queued, sent, ..., lost
  std::string source;  // auto | manual
  std::string requestedBy;
  domain::TimePoint createdAt;
  domain::TimePoint changedAt;
  std::string message;
  std::string cpu;
  std::int64_t remaining = 0;
  bool cancelRequested = false;
};

// Queues a one-off order. Fails for unknown goods, an amount <= 0, goods AE
// cannot craft, and goods with an open order already (one at a time: a CPU
// is matched to an order by its item).
auto orderOnce(pg::Cluster& db, std::int64_t goodsId, std::int64_t amount,
               std::string_view requestedBy, domain::TimePoint now)
    -> std::expected<std::int64_t, std::string>;

// The newest orders first.
auto listOrders(pg::Cluster& db, std::int64_t limit) -> std::vector<OrderRow>;

// Cancels an order (domain::requestCancel): a queued one at once, one in the
// agent's hands through it. Fails for an unknown order; for a closed one it
// does nothing.
auto cancelOrder(pg::Cluster& db, std::int64_t id, domain::TimePoint now)
    -> std::expected<domain::CancelAction, std::string>;

}  // namespace factoryctl::db
