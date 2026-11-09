#pragma once

// Checked reads of the agents' JSON fields: an error names the field's path
// (`items[3].size: negative`), never an exception.

#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <userver/formats/json/value.hpp>
#include <userver/logging/level.hpp>

#include "domain/poll.hpp"

namespace factoryctl::agent::fields {

namespace json = userver::formats::json;

template <typename T>
using Result = std::expected<T, std::string>;

inline auto fail(std::string_view path, std::string_view what)
    -> std::unexpected<std::string> {
  return std::unexpected(std::format("{}: {}", path, what));
}

// An integer; JSON 1000.0 counts (Lua may print an integral float so).
inline auto integer(const json::Value& v, std::string_view path)
    -> Result<std::int64_t> {
  if (v.IsMissing()) return fail(path, "missing");
  if (!v.IsInt64()) return fail(path, "not an integer");
  return v.As<std::int64_t>();
}

inline auto text(const json::Value& v, std::string_view path) -> Result<std::string> {
  if (v.IsMissing()) return fail(path, "missing");
  if (!v.IsString()) return fail(path, "not a string");
  return v.As<std::string>();
}

inline auto flag(const json::Value& v, std::string_view path, bool absent)
    -> Result<bool> {
  if (v.IsMissing()) return absent;
  if (!v.IsBool()) return fail(path, "not a boolean");
  return v.As<bool>();
}

inline auto maybeFlag(const json::Value& v, std::string_view path)
    -> Result<std::optional<bool>> {
  if (v.IsMissing()) return std::nullopt;
  if (!v.IsBool()) return fail(path, "not a boolean");
  return v.As<bool>();
}

inline auto logLine(const json::Value& v, std::string_view path)
    -> Result<domain::AgentLogLine> {
  if (!v.IsObject()) return fail(path, "not an object");
  domain::AgentLogLine l;
  const auto& t = v["t"];
  if (t.IsMissing() || !t.IsDouble()) {
    return fail(std::format("{}.t", path), "not a number");
  }
  l.t = t.As<double>();
  const auto level = text(v["level"], std::format("{}.level", path));
  if (!level) return std::unexpected(level.error());
  if (*level != "dbg" && *level != "inf" && *level != "wrn" &&
      *level != "err") {
    return fail(std::format("{}.level", path),
                std::format("unknown '{}'", *level));
  }
  l.level = *level;
  const auto body = text(v["text"], std::format("{}.text", path));
  if (!body) return std::unexpected(body.error());
  l.text = *body;
  return l;
}

// The level of an agent's log line (dbg | inf | wrn | err) as an event's.
inline auto eventLevel(std::string_view agentLevel) -> std::string_view {
  if (agentLevel == "err") return "error";
  if (agentLevel == "wrn") return "warning";
  if (agentLevel == "dbg") return "debug";
  return "info";
}

// ... and as the server log's.
inline auto logLevel(std::string_view agentLevel) -> userver::logging::Level {
  using userver::logging::Level;
  if (agentLevel == "err") return Level::kError;
  if (agentLevel == "wrn") return Level::kWarning;
  if (agentLevel == "dbg") return Level::kDebug;
  return Level::kInfo;
}

}  // namespace factoryctl::agent::fields
