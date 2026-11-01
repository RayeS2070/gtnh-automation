#pragma once

// JSON of the client API: what goes out and what comes in. Errors of the
// client's input are messages for the page, never exceptions.

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>

#include "db/cpus.hpp"
#include "db/events.hpp"
#include "db/manual.hpp"
#include "db/stock.hpp"
#include "db/targets.hpp"

namespace factoryctl::api {

// {"goodsId", "name", "damage", "variant", "label", "amount"}
auto toJson(const db::StockRow& row) -> userver::formats::json::ValueBuilder;

// The goods' fields and {"threshold", "batch", "priority", "enabled", "note",
// "fill", "group": a group's id or null}.
auto toJson(const db::TargetRow& row) -> userver::formats::json::ValueBuilder;

// The body of PUT /api/v1/targets/{goodsId}:
//   {"threshold": 100, "batch": 64, "priority": "high"|"low",
//    "enabled": true, "note": "...", "fill": false, "group": 3}
// threshold and batch are required; priority (high), enabled (true), note
// (''), fill (false) and group (null: none) may be left out.
auto parseTargetSettings(const userver::formats::json::Value& body)
    -> std::expected<db::TargetSettings, std::string>;

// {"id", "name", "targets": how many are in it}
auto toJson(const db::TargetGroup& g) -> userver::formats::json::ValueBuilder;

// The body of POST /api/v1/target-groups and PUT .../{id}: {"name": "..."},
// trimmed, 1 to 100 characters.
auto parseGroupName(const userver::formats::json::Value& body)
    -> std::expected<std::string, std::string>;

// The goods' fields under "goods", and {"id", "amount", "status", "source",
// "requestedBy", "createdAt", "changedAt", "message", "cpu", "remaining",
// "cancelRequested"}; times as UTC "2026-10-30T06:40:20Z".
auto toJson(const db::OrderRow& row) -> userver::formats::json::ValueBuilder;

// {"name", "busy", "goods": the goods' fields or null, "remaining",
//  "seenAt" (UTC, as for orders), "order": our open order's id or null}
auto toJson(const db::CpuRow& row) -> userver::formats::json::ValueBuilder;

// {"id", "at" (UTC), "level", "source", "text", "order" or null, "goods":
//  {"goodsId", "name", "damage", "variant", "label"} or null, "agentUptime"
//  or null}
auto toJson(const db::EventRow& row) -> userver::formats::json::ValueBuilder;

// The body of POST /api/v1/orders: {"goodsId": 1267, "amount": 128,
// "requestedBy": "Pavel"}; requestedBy may be left out.
struct OrderRequest {
  std::int64_t goodsId = 0;
  std::int64_t amount = 0;
  std::string requestedBy;
};

auto parseOrderRequest(const userver::formats::json::Value& body)
    -> std::expected<OrderRequest, std::string>;

// A positive id from a path argument.
auto parseId(std::string_view text) -> std::optional<std::int64_t>;

// {"error": "..."}
auto errorJson(std::string_view message) -> std::string;

}  // namespace factoryctl::api
