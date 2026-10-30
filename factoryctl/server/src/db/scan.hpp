#pragma once

// Scans of the ME network in the database: batches come in while the scan
// runs; only a finished scan says what is absent (= 0).

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <userver/storages/postgres/postgres_fwd.hpp>

#include "domain/order.hpp"
#include "domain/scan.hpp"

namespace factoryctl::db {

namespace pg = userver::storages::postgres;

auto startScan(pg::Cluster& db, domain::TimePoint now) -> std::int64_t;
auto startScan(pg::Transaction& tx, domain::TimePoint now) -> std::int64_t;

// Refused for an unknown or finished scan.
auto storeBatch(pg::Cluster& db, std::int64_t scan,
                std::span<const domain::ScannedGoods> goods)
    -> std::expected<void, std::string>;

// Closes the scan and writes the stock of every target into the history
// (0 for a target the scan did not see). Refused for an unknown or finished
// scan.
auto finishScan(pg::Cluster& db, std::int64_t scan, domain::TimePoint now)
    -> std::expected<void, std::string>;

// Stock as of the last finished scan, with what a running scan has stored
// since; nullopt before the first finished scan. Absent goods have 0.
auto currentStock(pg::Cluster& db) -> std::optional<domain::Stock>;

}  // namespace factoryctl::db
