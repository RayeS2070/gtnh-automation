#pragma once

// One poll of the agent is one step of the server: what the agent reports
// moves the orders on, then new work is handed out. Plain C++, no I/O: the
// handler loads the state, calls tick() and stores what it returns.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "domain/order.hpp"
#include "domain/plan.hpp"

namespace factoryctl::domain {

// How the agent scans, sent to it with every scan task.
struct ScanParams {
  std::int64_t threshold =
      500;  // stacks below it and not craftable are left out
  std::int32_t maxId = 11500;
  std::int32_t chunk = 1000;
  std::int32_t slowFrom = 250;  // ids with a great many NBT variants:
  std::int32_t slowTo = 320;  // small chunks, or the server runs out of memory
  std::int32_t slowChunk = 5;
};

// All periods are settings: short ones make debugging faster.
struct Settings {
  std::chrono::seconds poll{10};
  std::chrono::seconds scanEvery{300};
  std::chrono::seconds scanGiveUp{
      600};  // a scan not finished by then is abandoned
  Timeouts timeouts;
  ScanParams scan;
  std::int32_t lowFreeCpus = 8;  // low priority needs this many idle CPUs
};

struct ScanState {
  std::optional<TimePoint> lastFinished;  // start of the last finished scan
  std::optional<TimePoint> running;       // start of the newest unfinished one
};

// Whether to start a scan now: none finished for `scanEvery`, and none
// running (or the running one is older than `scanGiveUp`: the agent lost it).
auto needScan(const ScanState& scans, const Settings& settings, TimePoint now)
    -> bool;

struct AgentResult {
  std::int64_t order = 0;
  AgentStatus status = AgentStatus::kAccepted;
  std::string message;
  // Whether getCraftables found its item, when the agent asked AE.
  std::optional<bool> craftable;
};

// A target's stock as the agent measured it in this poll, after reading
// the CPUs. No size: the lookup found nothing (0, or a lookup that missed).
struct Probe {
  ItemKey item;
  std::optional<std::int64_t> size;
  std::optional<bool> craftable;  // isCraftable of what the lookup found
};

// What the agent learned about whether AE can craft an item.
struct Craftability {
  ItemKey item;
  bool craftable = true;

  auto operator==(const Craftability&) const -> bool = default;
};

// A line of the agent's log; `t` is its computer.uptime() (it has no clock).
struct AgentLogLine {
  double t = 0;
  std::string level;  // dbg | inf | wrn | err
  std::string text;
};

struct PollReport {
  std::vector<Cpu> cpus;
  std::vector<AgentResult> results;
  std::vector<Probe> stock;
  std::vector<AgentLogLine> log;
};

// What a probe tells about the stock: its size; "found nothing" is 0 only if
// the last scan agrees. A scan that saw some means the lookup missed it:
// trusting it would order without end, so the probe is ignored (nullopt).
auto trustedAmount(const Probe& probe, std::int64_t scanned)
    -> std::optional<std::int64_t>;

struct CancelTask {
  std::int64_t order = 0;
  std::string cpu;

  auto operator==(const CancelTask&) const -> bool = default;
};

struct Alarm {
  std::int64_t order = 0;
  AlarmKind kind = AlarmKind::kLost;

  auto operator==(const Alarm&) const -> bool = default;
};

struct TickInput {
  PollReport report;
  std::vector<Target> targets;  // enabled ones
  const Stock* stock = nullptr;
  // Goods with an order closed since their stock was last measured (from
  // the database); orders that close in this tick are added to them unless
  // this report probed them (measured after the CPUs: the result is in).
  std::vector<ItemKey> settling;
  // Goods AE could not craft when last seen (from the database); this
  // report's results and probes update it.
  std::vector<ItemKey> uncraftable;
  TimePoint now;
};

struct TickOutput {
  std::vector<std::int64_t> send;  // queued orders handed to the agent now
  std::vector<CancelTask> cancel;
  std::vector<NewOrder> created;  // queued; sent with the next poll
  std::vector<Alarm> alarms;
  // Learned in this report, in order (results, then probes): to store.
  std::vector<Craftability> craftability;
  std::vector<std::string> errors;  // results that did not fit, for the log
};

// `orders` are the open orders; they are changed in place (some may close).
// Order: results, then CPUs (a canceled craft gone from its CPU must not
// count as done), time limits, sending, cancel tasks, planning.
auto tick(std::vector<OrderState>& orders, const TickInput& in,
          const Settings& settings) -> TickOutput;

}  // namespace factoryctl::domain
