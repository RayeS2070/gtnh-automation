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

#include "db/stock.hpp"
#include "db/targets.hpp"

namespace factoryctl::api {

// {"goodsId", "name", "damage", "variant", "label", "amount"}
auto toJson(const db::StockRow& row) -> userver::formats::json::ValueBuilder;

// The goods' fields and {"threshold", "batch", "priority", "enabled", "note"}.
auto toJson(const db::TargetRow& row) -> userver::formats::json::ValueBuilder;

// The body of PUT /api/v1/targets/{goodsId}:
//   {"threshold": 100, "batch": 64, "priority": "high"|"low",
//    "enabled": true, "note": "..."}
// threshold and batch are required; priority (high), enabled (true) and note
// ('') may be left out.
auto parseTargetSettings(const userver::formats::json::Value& body)
    -> std::expected<db::TargetSettings, std::string>;

// A positive id from a path argument.
auto parseId(std::string_view text) -> std::optional<std::int64_t>;

// {"error": "..."}
auto errorJson(std::string_view message) -> std::string;

}  // namespace factoryctl::api
