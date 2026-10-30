#pragma once

// Stock as the client sees it.

#include <cstdint>
#include <string>
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

enum class GoodsKind { kAll, kItems, kFluids };
enum class StockOrder { kLabel, kAmount };

// A drop's label is matched and sorted without its "drop of " prefix.
struct StockQuery {
  std::string text;  // words, all found in the label in any order and case
  GoodsKind kind = GoodsKind::kAll;
  StockOrder order = StockOrder::kLabel;  // ties: by label
  bool descending = false;
  std::int64_t limit = 200;
};

// Before the first finished scan there is no stock: empty.
auto findStock(pg::Cluster& db, const StockQuery& query)
    -> std::vector<StockRow>;

}  // namespace factoryctl::db
