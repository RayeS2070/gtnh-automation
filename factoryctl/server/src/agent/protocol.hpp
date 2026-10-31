#pragma once

// What the agent sends. It builds the JSON by hand in Lua: every field is
// checked, and a bad message is an error with the path of the field, never
// an exception.

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <userver/formats/json/value.hpp>
#include <vector>

#include "domain/order.hpp"
#include "domain/poll.hpp"
#include "domain/scan.hpp"

namespace factoryctl::agent {

// POST /agent/v1/scan:
//   {"scan": 42, "last": false,
//    "items": [{"name": "...", "damage": 0, "label": "...", "size": 5,
//               "fluid": "molten.platinum"}]}
// `last` and `fluid` may be left out; `damage` too (0). `fluid` makes it a
// fluid: a drop (2.8.4) or, in 2.9, an AE fluid ({"name": "plasma.niobium",
// "fluid": "plasma.niobium"}); both get the same key.
struct ScanBatch {
  std::int64_t scan = 0;
  bool last = false;
  std::vector<domain::ScannedStack> stacks;
};

auto parseScan(const userver::formats::json::Value& body)
    -> std::expected<ScanBatch, std::string>;

// POST /agent/v1/poll:
//   {"cpus": [{"name": "CPU_022", "busy": true,
//              "output": {"name", "damage", "label", "size", "fluid"}}],
//    "results": [{"order": 17, "status": "accepted", "message": "..."}],
//    "stock": [{"name", "damage", "fluid", "size"}],
//    "log": [{"t": <uptime>, "level": "inf", "text": "..."}]}
// `output` is finalOutput(), left out for a free CPU; it is keyed like a
// scanned stack, its size is what is left to deliver (below 0 while a batch
// bigger than asked finishes). `stock` measures the
// probe list of the last reply, after the CPUs were read; no size: not found.
// `log` is the agent's log since the last poll that got through.
// `results`, `stock`, `log` and `message` may be left out. Statuses: computing,
// accepted, failed, canceled, done.
auto parsePoll(const userver::formats::json::Value& body)
    -> std::expected<domain::PollReport, std::string>;

// A scan the agent should run now.
struct ScanTask {
  std::int64_t id = 0;
  domain::ScanParams params;
};

// A target the agent measures in every poll; the label is for drops (a
// lookup by label is the fallback when the fluid lookup misses).
struct ProbeTask {
  domain::ItemKey item;
  std::string label;
};

struct PollReply {
  std::chrono::seconds poll{10};
  std::vector<ProbeTask> probe;
  std::vector<domain::OrderState> send;    // orders handed out now
  std::vector<domain::OrderState> cancel;  // crafts on a CPU (order.cpu)
  std::optional<ScanTask> scan;
};

// The answer to a poll, a Lua table:
//   {poll=10, orders={{id=18, amount=1000, filter={...}, fluid=...}},
//    cancel={{order=17, cpu="CPU_022", filter={...}, fluid=...}},
//    probe={{name=..., damage=...}, {name="ae2fc:fluid_drop", fluid=...,
//            label=...}},
//    scan={id=42, threshold=500, maxId=11500, chunk=1000,
//          slow={from=250, to=320, chunk=5}}}
// `filter` goes to getCraftables as is: {name, damage} for an item,
// {name, label} for a fluid drop (drops differ in NBT only). A fluid has
// `fluid` too, its name: in 2.9 the agent asks for the AE fluid by it instead
// of the drop. A cancel task has them too: the agent cancels only if the CPU
// still shows that item. No `scan` when there is none to run.
auto replyPoll(const PollReply& reply)
    -> std::expected<std::string, std::string>;

}  // namespace factoryctl::agent
