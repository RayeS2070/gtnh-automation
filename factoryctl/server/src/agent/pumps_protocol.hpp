#pragma once

// The pumping agent's protocol: JSON up, a Lua table literal down, as for the
// crafting agent.

#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <userver/formats/json/value.hpp>

#include "domain/pumps.hpp"

namespace factoryctl::agent {

// POST /agent/v1/pumps/poll:
//   {"run": "1a2b3c4d-1730000000", "settings": 3,
//    "offer": {"version", "batch", "threshold",
//              "targets": [{"fluid", "target", "priority", "enabled"}]},
//    "recipes": [{"planet", "gas", "fluid", "label", "rate"}],
//    "modules": [{"address", "tier", "state", "progress", "maxProgress",
//                 "slots": [{"fluid", "parallel"}]}],
//    "cycles": [{"run", "seq", "module", "batch", "result", "message",
//                "duration", "slots": [{"fluid", "parallel", "expected"}]}],
//    "stock": [{"fluid", "amount"}],
//    "log": [{"t", "level", "text"}]}
// `offer` comes when the agent has no settings of the server (or was asked
// for them), `recipes` until a poll of this run got through; `progress`,
// `maxProgress`, `message`, `duration` may be left out.
auto parsePumpsPoll(const userver::formats::json::Value& body)
    -> std::expected<domain::PumpReport, std::string>;

struct PumpsReply {
  std::chrono::seconds poll{10};
  std::optional<domain::PumpSettings> settings;  // when the agent's are older
  bool offer = false;  // the server has none: send yours
};

// {poll=10, settings={version=4, batch=30, threshold=0.95,
//                     targets={{fluid=..., target=..., priority=...,
//                               enabled=true}}}, offer=true}
auto replyPumpsPoll(const PumpsReply& reply)
    -> std::expected<std::string, std::string>;

}  // namespace factoryctl::agent
