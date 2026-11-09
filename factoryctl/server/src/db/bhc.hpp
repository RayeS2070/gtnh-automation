#pragma once

// Black hole setups: the agent's last report, its settings (edited in the
// client, taken by the agent) and its sessions.

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <userver/formats/json/value.hpp>
#include <userver/storages/postgres/postgres_fwd.hpp>
#include <vector>

#include "agent/bhc_protocol.hpp"
#include "domain/order.hpp"

namespace factoryctl::db {

namespace pg = userver::storages::postgres;

inline constexpr std::int64_t kBhcSessionsKept = 1000;  // per setup

struct BhcSetup {
  std::string name;
  std::optional<agent::Coords> coords;
  std::string run;
  userver::formats::json::Value status;  // state, supplies
  domain::TimePoint seenAt;
  std::int64_t settingsVersion = 0;  // 0: none
  std::optional<userver::formats::json::Value> settings;
};

struct BhcSessionRow {
  domain::TimePoint closedAt;
  agent::BhcSession s;
};

// The setup, locked; none when it never polled.
auto lockBhcSetup(pg::Transaction& tx, const std::string& name)
    -> std::optional<BhcSetup>;

// Creates or replaces the row.
auto saveBhcSetup(pg::Transaction& tx, const BhcSetup& s, domain::TimePoint now)
    -> void;

// Every setup, by name.
auto listBhcSetups(pg::Cluster& db) -> std::vector<BhcSetup>;

// Adds the sessions of a poll, each ended `ago` s before `now`; one already
// there (a poll sent again) is skipped. Returns the new ones. Keeps the newest
// kBhcSessionsKept of the setup.
auto addBhcSessions(pg::Transaction& tx, const std::string& setup,
                    const std::string& run,
                    std::span<const agent::BhcSession> sessions,
                    domain::TimePoint now) -> std::vector<agent::BhcSession>;

// The newest `limit` sessions of a setup, newest first.
auto listBhcSessions(pg::Cluster& db, const std::string& setup,
                     std::int64_t limit) -> std::vector<BhcSessionRow>;

// New settings from the client (agent::checkBhcSettings); the version goes
// up, the agent takes them with its next poll. No such setup: nullopt in the
// expected.
auto setBhcSettings(pg::Cluster& db, const std::string& name,
                    const userver::formats::json::Value& settings,
                    domain::TimePoint now, std::string_view by = {})
    -> std::expected<std::optional<std::int64_t>, std::string>;

// Forgets a setup and its sessions. Returns false when unknown.
auto deleteBhcSetup(pg::Cluster& db, const std::string& name,
                    domain::TimePoint now, std::string_view by = {}) -> bool;

// "hold at 20, floor 2, 120 holds, 10000 L to open"
auto describeBhcSettings(const userver::formats::json::Value& s) -> std::string;

}  // namespace factoryctl::db
