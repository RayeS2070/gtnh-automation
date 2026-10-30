#pragma once

// Stock as the client sees it.

#include <cstdint>
#include <string>
#include <string_view>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <vector>

#include "domain/plan.hpp"

namespace factoryctl::db {

namespace pg = userver::storages::postgres;

struct StockRow {
  std::int64_t goodsId = 0;
  domain::ItemKey key;
  std::string label;
  std::int64_t amount = 0;  // 0 when the last finished scan did not see it
};

// Goods whose label contains `query` (any case; empty: all), by label,
// at most `limit`. Before the first finished scan there is no stock: empty.
auto findStock(pg::Cluster& db, std::string_view query, std::int64_t limit)
    -> std::vector<StockRow>;

}  // namespace factoryctl::db
