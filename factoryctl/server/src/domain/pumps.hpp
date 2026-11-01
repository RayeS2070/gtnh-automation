#pragma once

// The pumping agent (factoryctl/pumps): it decides by itself which fluid its
// Space Elevator pump modules pump; the server keeps its settings and what it
// reports. Fluids by their internal name, as ItemKey::variant.

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "domain/poll.hpp"

namespace factoryctl::domain {

struct PumpTarget {
  std::string fluid;
  std::int64_t target = 0;  // mB to keep; 0: never pumped
  double priority = 1;      // > 0: the agent ranks by 1 - fill^priority
  bool enabled = true;

  auto operator==(const PumpTarget&) const -> bool = default;
};

struct PumpSettings {
  std::int64_t version = 0;  // goes up with every change; 0: none yet
  std::int32_t batch = 30;   // seconds of a cycle, 1..128
  double threshold = 0.95;   // a fluid below threshold * target is pumped
  std::vector<PumpTarget> targets;

  auto operator==(const PumpSettings&) const -> bool = default;
};

// Batch and threshold in range; targets with a fluid, once each, target >= 0,
// priority > 0. The version is not looked at.
auto checkPumpSettings(const PumpSettings& s) -> std::expected<void, std::string>;

// What a module can pump (the agent's fp/recipes.lua).
struct PumpRecipe {
  std::int32_t planet = 0;
  std::int32_t gas = 0;
  std::string fluid;
  std::string label;
  std::int64_t rate = 0;  // mB per second of a cycle, one parallel

  auto operator==(const PumpRecipe&) const -> bool = default;
};

struct PumpSlot {
  std::string fluid;
  std::int32_t parallel = 0;
  std::int64_t expected = 0;  // mB of one cycle: cycles only

  auto operator==(const PumpSlot&) const -> bool = default;
};

struct PumpModule {
  std::string address;
  std::int32_t tier = 0;
  std::string state;  // idle | pulse | starting | running | backoff
  std::vector<PumpSlot> slots;  // of the cycle it runs
  std::optional<std::int64_t> progress, maxProgress;  // ticks, running only

  auto operator==(const PumpModule&) const -> bool = default;
};

// A cycle that ended, once per (run, seq): a lost reply sends it again.
struct PumpCycle {
  std::string run;  // a run of the agent
  std::int64_t seq = 0;
  std::string module;
  std::int32_t batch = 0;
  std::string result;  // done | failed
  std::string message;
  std::optional<double> duration;  // s, done ones
  std::vector<PumpSlot> slots;

  auto operator==(const PumpCycle&) const -> bool = default;
};

struct FluidAmount {
  std::string fluid;
  std::int64_t amount = 0;

  auto operator==(const FluidAmount&) const -> bool = default;
};

struct PumpReport {
  std::string run;
  std::int64_t settings = 0;  // the version the agent runs on
  std::optional<PumpSettings> offer;  // its own: for a server that has none
  std::optional<std::vector<PumpRecipe>> recipes;  // once per run
  std::vector<PumpModule> modules;
  std::vector<PumpCycle> cycles;
  std::vector<FluidAmount> stock;  // the pumping network, target fluids
  std::vector<AgentLogLine> log;
};

}  // namespace factoryctl::domain
