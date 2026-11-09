#pragma once

// The black hole agent's protocol (factoryctl/bhc): JSON up, a Lua table
// literal down. The agent decides by itself, second by second; it polls only
// while its hole is closed. The server keeps per setup (one BHC, one computer)
// its settings, the last report and the sessions.

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <userver/formats/json/value.hpp>
#include <vector>

#include "agent/qft_protocol.hpp"  // Coords
#include "domain/poll.hpp"

namespace factoryctl::agent {

// POST /agent/v1/bhc/poll:
//   {"setup": {"name": "bhc@1,2,3", "coords": {"x", "y", "z"}},
//    "run": "...", "settings": 3, "offer": <settings>,
//    "state": {"phase", "session", "stability", "holds", "spent", "blocked",
//              "low", "busy", "work"},
//    "supplies": {"seeds", "collapsers", "spacetime"},
//    "sessions": [{"session", "seconds", "holds", "spent", "recipes",
//                  "ending", "unknown", "blocked", "ago"}],
//    "log": [...]}
// settings: {"holdAt", "floor", "maxHolds", "minSpacetime"} (version aside).
// A session goes until a poll with it got through; "ago" is the seconds since
// it ended. `coords`, `offer`, `supplies`, `sessions`, `log` may be left out.
struct BhcSession {
  std::int64_t session = 0;
  std::int64_t seconds = 0;  // the hole was open
  std::int64_t holds = 0;
  std::int64_t spent = 0;  // L of spacetime the holds took
  std::int64_t recipes = 0;
  // collapsed | closed (not by the agent) | seed (not taken)
  std::string ending;
  bool unknown = false;
  bool blocked = false;
  double ago = 0;
};

struct BhcReport {
  std::string name;
  std::optional<Coords> coords;
  std::string run;
  std::int64_t settings = 0;
  std::optional<userver::formats::json::Value> offer;  // checked
  userver::formats::json::Value status;                // {"state", "supplies"}
  std::vector<BhcSession> sessions;
  std::vector<domain::AgentLogLine> log;
};

auto parseBhcPoll(const userver::formats::json::Value& body)
    -> std::expected<BhcReport, std::string>;

// Settings as the agent takes them (bhc/link.lua checks the same): integers,
// 1 <= floor < holdAt <= 99, 0 <= maxHolds <= kBhcMaxHolds, minSpacetime >= 0.
// Unknown fields are refused: a typo would be lost silently.
inline constexpr std::int64_t kBhcMaxHolds = 600;
auto checkBhcSettings(const userver::formats::json::Value& settings)
    -> std::expected<void, std::string>;

struct BhcReply {
  std::chrono::seconds poll{30};
  std::optional<userver::formats::json::Value> settings;  // with "version"
  bool offer = false;
};

// {poll=30, settings={version=4, holdAt=20, ...}, offer=true}
auto replyBhcPoll(const BhcReply& reply)
    -> std::expected<std::string, std::string>;

}  // namespace factoryctl::agent
