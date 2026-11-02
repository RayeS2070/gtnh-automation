#pragma once

// Mining: the setups (one computer's modules each) and the targets.

#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <userver/formats/json/value.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <vector>

#include "db/stock.hpp"
#include "domain/mining.hpp"
#include "domain/order.hpp"

namespace factoryctl::db {

namespace pg = userver::storages::postgres;

struct MiningSetup {
  std::string name;
  std::string run;
  userver::formats::json::Value status;  // the last report
  userver::formats::json::Value plan;    // [{module, setting, value, rates}]
  domain::TimePoint seenAt;
};

auto lockMiningSetup(pg::Transaction& tx, const std::string& name)
    -> std::optional<MiningSetup>;
auto saveMiningSetup(pg::Transaction& tx, const MiningSetup& s) -> void;
// Every setup but `except`, by name.
auto listMiningSetups(pg::Transaction& tx, std::string_view except = {})
    -> std::vector<MiningSetup>;
auto listMiningSetups(pg::Cluster& db) -> std::vector<MiningSetup>;
// Returns false when unknown.
auto deleteMiningSetup(pg::Cluster& db, const std::string& name,
                       domain::TimePoint now, std::string_view by = {}) -> bool;

struct MiningTargetRow {
  StockRow goods;  // the product, with its stock
  domain::MiningTarget target;
};

auto listMiningTargets(pg::Transaction& tx) -> std::vector<MiningTargetRow>;
auto listMiningTargets(pg::Cluster& db) -> std::vector<MiningTargetRow>;

struct MiningTargetSettings {
  std::int64_t threshold = 0;
  double priority = 1;
  bool enabled = true;
  std::vector<domain::ItemKey> ores;  // damage and name only
};

// Creates or replaces; fails for unknown goods or bad numbers.
auto setMiningTarget(pg::Cluster& db, std::int64_t goodsId,
                     const MiningTargetSettings& s, domain::TimePoint now,
                     std::string_view by = {})
    -> std::expected<void, std::string>;
auto deleteMiningTarget(pg::Cluster& db, std::int64_t goodsId,
                        domain::TimePoint now, std::string_view by = {}) -> bool;

// The goods with that id, its stock with it.
auto goodsById(pg::Cluster& db, std::int64_t id) -> std::optional<StockRow>;

// The stock of the fluids named (variants of fluid drops: domain::keyOf).
auto fluidStock(pg::Transaction& tx, const std::vector<std::string>& fluids)
    -> std::map<std::string, double>;

}  // namespace factoryctl::db
