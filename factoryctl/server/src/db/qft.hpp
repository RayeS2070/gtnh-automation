#pragma once

// QFT setups: the agent's last report and its settings (edited in the client,
// taken by the agent).

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <userver/formats/json/value.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <vector>

#include "agent/qft_protocol.hpp"
#include "domain/order.hpp"

namespace factoryctl::db {

namespace pg = userver::storages::postgres;

struct QftSetup {
  std::string name;
  std::optional<agent::Coords> coords;
  std::string run;
  userver::formats::json::Value catalog;  // an array
  userver::formats::json::Value status;   // state, recipes, plasma
  domain::TimePoint seenAt;
  std::int64_t settingsVersion = 0;  // 0: none
  std::optional<userver::formats::json::Value> settings;
};

// The setup, locked; none when it never polled.
auto lockQftSetup(pg::Transaction& tx, const std::string& name)
    -> std::optional<QftSetup>;

// Creates or replaces the row.
auto saveQftSetup(pg::Transaction& tx, const QftSetup& s, domain::TimePoint now)
    -> void;

// Every setup, by name.
auto listQftSetups(pg::Cluster& db) -> std::vector<QftSetup>;

// New settings from the client, checked against the setup's catalog; the
// version goes up, the agent takes them with its next poll. No such setup:
// nullopt in the expected.
auto setQftSettings(pg::Cluster& db, const std::string& name,
                    const userver::formats::json::Value& settings,
                    domain::TimePoint now)
    -> std::expected<std::optional<std::int64_t>, std::string>;

// Forgets a setup (renamed, rebuilt elsewhere). Returns false when unknown.
auto deleteQftSetup(pg::Cluster& db, const std::string& name,
                    domain::TimePoint now) -> bool;

}  // namespace factoryctl::db
