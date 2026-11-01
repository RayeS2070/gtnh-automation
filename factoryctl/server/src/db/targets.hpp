#pragma once

// Targets as the client edits them (the poll reads them through
// db::loadTargets).

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <vector>

#include "db/stock.hpp"
#include "domain/order.hpp"
#include "domain/plan.hpp"

namespace factoryctl::db {

namespace pg = userver::storages::postgres;

struct TargetSettings {
  std::int64_t threshold = 0;  // 0: order whenever nothing is on order
  std::int64_t batch = 0;
  domain::Priority priority = domain::Priority::kHigh;
  bool enabled = true;
  std::string note;
  bool fill = false;  // order what is missing, in whole batches
  std::optional<std::int64_t> group;  // a TargetGroup, for people only

  auto operator==(const TargetSettings&) const -> bool = default;
};

struct TargetGroup {
  std::int64_t id = 0;
  std::string name;
  std::int64_t targets = 0;  // how many are in it
};

struct TargetRow {
  StockRow goods;  // with the current amount
  TargetSettings settings;
};

// Every target, enabled or not, by label.
auto listTargets(pg::Cluster& db) -> std::vector<TargetRow>;

// Creates or replaces the target of the goods, and tells it (events). Fails
// for unknown goods or an unknown group.
auto setTarget(pg::Cluster& db, std::int64_t goodsId, const TargetSettings& s,
               domain::TimePoint now) -> std::expected<void, std::string>;

// Returns false when there was no target.
auto deleteTarget(pg::Cluster& db, std::int64_t goodsId, domain::TimePoint now)
    -> bool;

// Every group, by name.
auto listGroups(pg::Cluster& db) -> std::vector<TargetGroup>;

// The new group's id. Fails for an empty name or one taken.
auto addGroup(pg::Cluster& db, const std::string& name, domain::TimePoint now)
    -> std::expected<std::int64_t, std::string>;

// false: no such group. Fails for an empty name or one taken.
auto renameGroup(pg::Cluster& db, std::int64_t id, const std::string& name,
                 domain::TimePoint now) -> std::expected<bool, std::string>;

// Its targets stay, ungrouped. Returns false when there was no such group.
auto deleteGroup(pg::Cluster& db, std::int64_t id, domain::TimePoint now)
    -> bool;

// The settings for a person: "keep 100000, order 5000, low priority, fill".
auto describe(const TargetSettings& s) -> std::string;

}  // namespace factoryctl::db
