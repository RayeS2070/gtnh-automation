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
};

struct PollReport {
  std::vector<Cpu> cpus;
  std::vector<AgentResult> results;
};

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
  TimePoint now;
};

struct TickOutput {
  std::vector<std::int64_t> send;  // queued orders handed to the agent now
  std::vector<CancelTask> cancel;
  std::vector<NewOrder> created;  // queued; sent with the next poll
  std::vector<Alarm> alarms;
  std::vector<std::string> errors;  // results that did not fit, for the log
};

// `orders` are the open orders; they are changed in place (some may close).
// Order: results, then CPUs (a canceled craft gone from its CPU must not
// count as done), time limits, sending, cancel tasks, planning.
auto tick(std::vector<OrderState>& orders, const TickInput& in,
          const Settings& settings) -> TickOutput;

}  // namespace factoryctl::domain
