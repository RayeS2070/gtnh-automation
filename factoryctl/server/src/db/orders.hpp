#pragma once

// Orders, targets and CPUs in the database, for the agent's poll. Functions
// take a transaction: a poll reads and writes them as one step.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <vector>

#include "domain/order.hpp"
#include "domain/plan.hpp"
#include "domain/poll.hpp"

namespace factoryctl::db {

namespace pg = userver::storages::postgres;

// Open orders, locked until the transaction ends: a cancel from a client
// waits for the poll instead of being overwritten by it.
auto loadOpenOrders(pg::Transaction& tx) -> std::vector<domain::OrderState>;

// Writes back what tick() changed (status, CPU, progress, flags).
auto saveOrders(pg::Transaction& tx, std::span<const domain::OrderState> orders)
    -> void;

// New automatic orders, queued. One whose goods have an open order already
// (a manual one made meanwhile) is skipped. Returns how many were made.
auto insertOrders(pg::Transaction& tx, std::span<const domain::NewOrder> orders,
                  domain::TimePoint now) -> std::int64_t;

// Enabled targets.
auto loadTargets(pg::Transaction& tx) -> std::vector<domain::Target>;

// Replaces the CPUs of the last poll.
auto saveCpus(pg::Transaction& tx, std::span<const domain::Cpu> cpus,
              domain::TimePoint now) -> void;

auto scanState(pg::Transaction& tx) -> domain::ScanState;

// Goods with an order closed after their stock was last measured: by the
// last finished scan (started at `stockAsOf`) or by a probe, whichever is
// later. That stock does not show what the order made.
auto loadSettling(pg::Transaction& tx, domain::TimePoint stockAsOf)
    -> std::vector<domain::ItemKey>;

struct ProbedAmount {
  domain::ItemKey item;
  std::int64_t amount = 0;
};

// Stock measured by the agent in a poll: the amount, when (for settling),
// and it counts as seen by the last finished scan. Unknown goods are left
// out (a probe is of a target, and targets are of scanned goods).
auto applyProbes(pg::Transaction& tx, std::span<const ProbedAmount> probes,
                 domain::TimePoint now) -> void;

// Names of the order_status database enum.
auto statusName(domain::OrderStatus status) -> std::string;
auto statusOf(std::string_view name) -> domain::OrderStatus;

}  // namespace factoryctl::db
