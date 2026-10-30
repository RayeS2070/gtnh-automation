#pragma once

// Targets as the client edits them (the poll reads them through
// db::loadTargets).

#include <cstdint>
#include <expected>
#include <string>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <vector>

#include "db/stock.hpp"
#include "domain/plan.hpp"

namespace factoryctl::db {

namespace pg = userver::storages::postgres;

struct TargetSettings {
  std::int64_t threshold = 0;  // 0: order whenever nothing is on order
  std::int64_t batch = 0;
  domain::Priority priority = domain::Priority::kHigh;
  bool enabled = true;
  std::string note;

  auto operator==(const TargetSettings&) const -> bool = default;
};

struct TargetRow {
  StockRow goods;  // with the current amount
  TargetSettings settings;
};

// Every target, enabled or not, by label.
auto listTargets(pg::Cluster& db) -> std::vector<TargetRow>;

// Creates or replaces the target of the goods. Fails for unknown goods.
auto setTarget(pg::Cluster& db, std::int64_t goodsId, const TargetSettings& s)
    -> std::expected<void, std::string>;

// Returns false when there was no target.
auto deleteTarget(pg::Cluster& db, std::int64_t goodsId) -> bool;

}  // namespace factoryctl::db
