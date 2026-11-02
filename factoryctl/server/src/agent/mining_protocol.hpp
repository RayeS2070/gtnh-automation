#pragma once

// The mining agent's protocol (factoryctl/mining): JSON up, a Lua table
// literal down. Unlike the pumps and the QFT, the server decides: it plans
// every module (domain::planMining) and the agent carries the plan out, with
// its own safety (the buffer, a module that does not start).

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <userver/formats/json/value.hpp>
#include <vector>

#include "agent/qft_protocol.hpp"
#include "domain/mining.hpp"
#include "domain/poll.hpp"

namespace factoryctl::agent {

// POST /agent/v1/mining/poll:
//   {"setup": "mining-1", "run": "...",
//    "modules": [{"x", "y", "z", "tier": 1..3, "state": "idle|loading|running|
//                 stopping|unloading|failed", "active": bool,
//                 "drone": tier (in its bus; absent: none),
//                 "assignment": "id" (absent: none), "error": "..."}],
//    "pool": [{"tier", "count"}],
//    "buffer": [{"name", "damage", "size"}],
//    "routing": {"slots": 63},
//    "log": [...]}
// The x, y, z of a module are its controller's: its key, adapters come and go.
struct MiningModuleReport {
  Coords at;
  int tier = 1;
  std::string state;
  bool active = false;
  std::optional<int> drone;
  std::optional<std::string> assignment;
  std::string error;
};

struct MiningReport {
  std::string name;
  std::string run;
  std::vector<MiningModuleReport> modules;
  std::map<int, int> pool;                        // drone tier -> count
  std::map<domain::ItemKey, double> buffer;       // ore -> items
  std::optional<int> slots;                       // of the keeping storage bus
  std::vector<domain::AgentLogLine> log;
  userver::formats::json::Value status;           // the report, for the web
};

auto parseMiningPoll(const userver::formats::json::Value& body)
    -> std::expected<MiningReport, std::string>;

auto moduleId(const Coords& at) -> std::string;  // "x,y,z"

// The id of a setting the agent compares with what it runs: "9/121/3/4".
auto settingId(const domain::MiningSetting& s) -> std::string;

struct MiningReply {
  std::chrono::seconds poll{10};
  struct Module {
    Coords at;
    std::optional<domain::MiningSetting> setting;  // none: idle
  };
  std::vector<Module> modules;
  std::vector<domain::ItemKey> keep;  // the storage bus whitelist
  double bufferLimit = 0;
};

// {poll=10, modules={{x=, y=, z=, id="9/121/3/4", droneTier=9,
//   drone={name=, damage=}, tip={...}, rod={...}, plasma="plasma.radon",
//   distance=121, parallel=4} or {x=, y=, z=, idle=true}},
//  keep={{name=, damage=}, ...}, buffer={limit=100000}}
auto replyMiningPoll(const MiningReply& reply,
                     const domain::MiningCatalog& catalog)
    -> std::expected<std::string, std::string>;

}  // namespace factoryctl::agent
