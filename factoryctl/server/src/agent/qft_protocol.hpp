#pragma once

// The QFT agent's protocol (factoryctl/qft): JSON up, a Lua table literal
// down. The agent decides by itself; the server keeps per setup (one QFT, one
// computer) its settings and shows what it reports. Most of the report is for
// the web only and is kept as JSON.

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <userver/formats/json/value.hpp>
#include <vector>

#include "domain/poll.hpp"

namespace factoryctl::agent {

struct Coords {
  std::int64_t x = 0, y = 0, z = 0;

  auto operator==(const Coords&) const -> bool = default;
};

// POST /agent/v1/qft/poll:
//   {"setup": {"name": "qft-main", "coords": {"x", "y", "z"}},
//    "run": "...", "settings": 3,
//    "offer": <settings>, "catalog": [<recipe>],
//    "state": {"phase", "mode", "want", "recipe", "fermium", "neptunium",
//              "cycleSeconds"},
//    "recipes": [{"id", "trigger", "low", "done", "disabled", "urgency",
//                 "cycles", "outputs": [{"circuit", "amount", "target"}]}],
//    "plasma": {"neptunium", "fermium"}, "log": [...]}
// settings: {"mode": "auto|deficit|surplus", "recipes": [{"id", "enabled",
//   "trigger", "outputs": [{"circuit", "target", "priority",
//   "weight"}]}]} (version aside); "enabled": false keeps a recipe from
//   running (absent: true); "trigger": "deficit|surplus" (absent: deficit).
// catalog recipe: {"id", "name", "tier", "catalysts", "inputs",
//   "outputs": [{"circuit", "kind", "label"}]}; an older agent adds the
//   trigger and the values of the outputs.
// `coords`, `offer`, `catalog` may be left out.
struct QftReport {
  std::string name;
  std::optional<Coords> coords;
  std::string run;
  std::int64_t settings = 0;
  std::optional<userver::formats::json::Value> offer;    // checked
  std::optional<userver::formats::json::Value> catalog;  // an array
  userver::formats::json::Value status;  // {"state", "recipes", "plasma"}
  std::vector<domain::AgentLogLine> log;
};

auto parseQftPoll(const userver::formats::json::Value& body)
    -> std::expected<QftReport, std::string>;

// Settings as the agent takes them (see above): a known mode, recipes with an
// id, outputs with a circuit, target and priority > 0, weight >= 0. With a
// catalog (not empty), every recipe and circuit must be in it. Fields the
// format does not have are refused: a typo would be lost silently.
auto checkQftSettings(const userver::formats::json::Value& settings,
                      const userver::formats::json::Value& catalog)
    -> std::expected<void, std::string>;

// Recipes of the catalog the settings do not have yet, added with the
// catalog's values (a recipe added to the agent later). None
// missing: nullopt.
auto mergeCatalog(const userver::formats::json::Value& settings,
                  const userver::formats::json::Value& catalog)
    -> std::optional<userver::formats::json::Value>;

struct QftReply {
  std::chrono::seconds poll{10};
  std::optional<userver::formats::json::Value> settings;  // with "version"
  bool offer = false;
};

// {poll=10, settings={version=4, mode="auto", recipes={...}}, offer=true}
auto replyQftPoll(const QftReply& reply)
    -> std::expected<std::string, std::string>;

}  // namespace factoryctl::agent
