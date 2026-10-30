#include "agent/protocol.hpp"

#include <format>
#include <userver/formats/json/value_builder.hpp>

#include "agent/lua.hpp"

namespace factoryctl::agent {

namespace {

namespace json = userver::formats::json;

template <typename T>
using Result = std::expected<T, std::string>;

auto fail(std::string_view path, std::string_view what)
    -> std::unexpected<std::string> {
  return std::unexpected(std::format("{}: {}", path, what));
}

// An integer; JSON 1000.0 counts (Lua may print an integral float so).
auto integer(const json::Value& v, std::string_view path)
    -> Result<std::int64_t> {
  if (v.IsMissing()) return fail(path, "missing");
  if (!v.IsInt64()) return fail(path, "not an integer");
  return v.As<std::int64_t>();
}

auto text(const json::Value& v, std::string_view path) -> Result<std::string> {
  if (v.IsMissing()) return fail(path, "missing");
  if (!v.IsString()) return fail(path, "not a string");
  return v.As<std::string>();
}

auto flag(const json::Value& v, std::string_view path, bool absent)
    -> Result<bool> {
  if (v.IsMissing()) return absent;
  if (!v.IsBool()) return fail(path, "not a boolean");
  return v.As<bool>();
}

auto maybeFlag(const json::Value& v, std::string_view path)
    -> Result<std::optional<bool>> {
  if (v.IsMissing()) return std::nullopt;
  if (!v.IsBool()) return fail(path, "not a boolean");
  return v.As<bool>();
}

auto stack(const json::Value& v, std::string_view path)
    -> Result<domain::ScannedStack> {
  if (!v.IsObject()) return fail(path, "not an object");
  domain::ScannedStack s;

  const auto name = text(v["name"], std::format("{}.name", path));
  if (!name) return std::unexpected(name.error());
  if (name->empty()) return fail(std::format("{}.name", path), "empty");
  s.name = *name;

  if (!v["damage"].IsMissing()) {
    const auto damage = integer(v["damage"], std::format("{}.damage", path));
    if (!damage) return std::unexpected(damage.error());
    s.damage = static_cast<std::int32_t>(*damage);
  }

  const auto label = text(v["label"], std::format("{}.label", path));
  if (!label) return std::unexpected(label.error());
  s.label = *label;

  const auto size = integer(v["size"], std::format("{}.size", path));
  if (!size) return std::unexpected(size.error());
  if (*size < 0) return fail(std::format("{}.size", path), "negative");
  s.size = *size;

  if (!v["fluid"].IsMissing()) {
    const auto fluid = text(v["fluid"], std::format("{}.fluid", path));
    if (!fluid) return std::unexpected(fluid.error());
    s.fluid = *fluid;
  }

  const auto craftable =
      flag(v["craftable"], std::format("{}.craftable", path), true);
  if (!craftable) return std::unexpected(craftable.error());
  s.craftable = *craftable;
  return s;
}

}  // namespace

auto parseScan(const json::Value& body) -> Result<ScanBatch> {
  if (!body.IsObject()) return fail("scan message", "not an object");
  ScanBatch batch;

  const auto scan = integer(body["scan"], "scan");
  if (!scan) return std::unexpected(scan.error());
  batch.scan = *scan;

  const auto last = flag(body["last"], "last", false);
  if (!last) return std::unexpected(last.error());
  batch.last = *last;

  const auto& items = body["items"];
  if (items.IsMissing()) return fail("items", "missing");
  if (!items.IsArray()) return fail("items", "not an array");
  batch.stacks.reserve(items.GetSize());
  for (std::size_t i = 0; i < items.GetSize(); ++i) {
    auto s = stack(items[i], std::format("items[{}]", i));
    if (!s) return std::unexpected(s.error());
    batch.stacks.push_back(*std::move(s));
  }
  return batch;
}

namespace {

auto status(const json::Value& v, std::string_view path)
    -> Result<domain::AgentStatus> {
  using domain::AgentStatus;
  const auto name = text(v, path);
  if (!name) return std::unexpected(name.error());
  if (*name == "computing") return AgentStatus::kComputing;
  if (*name == "accepted") return AgentStatus::kAccepted;
  if (*name == "failed") return AgentStatus::kFailed;
  if (*name == "canceled") return AgentStatus::kCanceled;
  if (*name == "done") return AgentStatus::kDone;
  return fail(path, std::format("unknown status '{}'", *name));
}

auto cpu(const json::Value& v, std::string_view path) -> Result<domain::Cpu> {
  if (!v.IsObject()) return fail(path, "not an object");
  domain::Cpu c;
  const auto name = text(v["name"], std::format("{}.name", path));
  if (!name) return std::unexpected(name.error());
  c.name = *name;
  if (v["busy"].IsMissing())
    return fail(std::format("{}.busy", path), "missing");
  const auto busy = flag(v["busy"], std::format("{}.busy", path), false);
  if (!busy) return std::unexpected(busy.error());
  c.busy = *busy;
  if (!v["output"].IsMissing()) {
    // finalOutput() is a stack like the scanned ones; its size is what is
    // left to deliver.
    const auto out = stack(v["output"], std::format("{}.output", path));
    if (!out) return std::unexpected(out.error());
    c.crafting = domain::keyOf(*out);
    c.remaining = out->size;
  }
  return c;
}

auto result(const json::Value& v, std::string_view path)
    -> Result<domain::AgentResult> {
  if (!v.IsObject()) return fail(path, "not an object");
  domain::AgentResult r;
  const auto order = integer(v["order"], std::format("{}.order", path));
  if (!order) return std::unexpected(order.error());
  r.order = *order;
  const auto st = status(v["status"], std::format("{}.status", path));
  if (!st) return std::unexpected(st.error());
  r.status = *st;
  if (!v["message"].IsMissing()) {
    const auto m = text(v["message"], std::format("{}.message", path));
    if (!m) return std::unexpected(m.error());
    r.message = *m;
  }
  const auto craftable =
      maybeFlag(v["craftable"], std::format("{}.craftable", path));
  if (!craftable) return std::unexpected(craftable.error());
  r.craftable = *craftable;
  return r;
}

auto probe(const json::Value& v, std::string_view path)
    -> Result<domain::Probe> {
  if (!v.IsObject()) return fail(path, "not an object");
  domain::ScannedStack s;
  const auto name = text(v["name"], std::format("{}.name", path));
  if (!name) return std::unexpected(name.error());
  s.name = *name;
  if (!v["damage"].IsMissing()) {
    const auto damage = integer(v["damage"], std::format("{}.damage", path));
    if (!damage) return std::unexpected(damage.error());
    s.damage = static_cast<std::int32_t>(*damage);
  }
  if (!v["fluid"].IsMissing()) {
    const auto fluid = text(v["fluid"], std::format("{}.fluid", path));
    if (!fluid) return std::unexpected(fluid.error());
    s.fluid = *fluid;
  }
  // keyOf would fall back to the label, which a probe does not carry.
  if (s.name == "ae2fc:fluid_drop" && s.fluid.empty()) {
    return fail(std::format("{}.fluid", path),
                "missing: a drop is keyed by it");
  }
  domain::Probe p{domain::keyOf(s), std::nullopt, std::nullopt};
  if (!v["size"].IsMissing()) {
    const auto size = integer(v["size"], std::format("{}.size", path));
    if (!size) return std::unexpected(size.error());
    if (*size < 0) return fail(std::format("{}.size", path), "negative");
    p.size = *size;
  }
  const auto craftable =
      maybeFlag(v["craftable"], std::format("{}.craftable", path));
  if (!craftable) return std::unexpected(craftable.error());
  p.craftable = *craftable;
  return p;
}

auto logLine(const json::Value& v, std::string_view path)
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

auto filter(const domain::OrderState& o) -> json::ValueBuilder {
  json::ValueBuilder f;
  f["name"] = o.item.name;
  // A drop is found by its label: all drops share name and damage.
  if (o.item.variant.empty()) {
    f["damage"] = o.item.damage;
  } else {
    f["label"] = o.label;
  }
  return f;
}

}  // namespace

auto parsePoll(const json::Value& body) -> Result<domain::PollReport> {
  if (!body.IsObject()) return fail("poll message", "not an object");
  domain::PollReport report;

  const auto& cpus = body["cpus"];
  if (cpus.IsMissing()) return fail("cpus", "missing");
  if (!cpus.IsArray()) return fail("cpus", "not an array");
  for (std::size_t i = 0; i < cpus.GetSize(); ++i) {
    auto c = cpu(cpus[i], std::format("cpus[{}]", i));
    if (!c) return std::unexpected(c.error());
    report.cpus.push_back(*std::move(c));
  }

  const auto& results = body["results"];
  if (!results.IsMissing()) {
    if (!results.IsArray()) return fail("results", "not an array");
    for (std::size_t i = 0; i < results.GetSize(); ++i) {
      auto r = result(results[i], std::format("results[{}]", i));
      if (!r) return std::unexpected(r.error());
      report.results.push_back(*std::move(r));
    }
  }
  const auto& log = body["log"];
  if (!log.IsMissing()) {
    if (!log.IsArray()) return fail("log", "not an array");
    for (std::size_t i = 0; i < log.GetSize(); ++i) {
      auto l = logLine(log[i], std::format("log[{}]", i));
      if (!l) return std::unexpected(l.error());
      report.log.push_back(*std::move(l));
    }
  }

  const auto& stock = body["stock"];
  if (!stock.IsMissing()) {
    if (!stock.IsArray()) return fail("stock", "not an array");
    for (std::size_t i = 0; i < stock.GetSize(); ++i) {
      auto p = probe(stock[i], std::format("stock[{}]", i));
      if (!p) return std::unexpected(p.error());
      report.stock.push_back(*std::move(p));
    }
  }
  return report;
}

auto replyPoll(const PollReply& reply) -> Result<std::string> {
  using userver::formats::common::Type;
  json::ValueBuilder b;
  b["poll"] = reply.poll.count();

  json::ValueBuilder orders(Type::kArray);
  for (const auto& o : reply.send) {
    json::ValueBuilder e;
    e["id"] = o.id;
    e["amount"] = o.amount;
    e["filter"] = filter(o);
    orders.PushBack(std::move(e));
  }
  b["orders"] = std::move(orders);

  json::ValueBuilder cancel(Type::kArray);
  for (const auto& o : reply.cancel) {
    json::ValueBuilder e;
    e["order"] = o.id;
    e["cpu"] = o.cpu;
    e["filter"] = filter(o);
    cancel.PushBack(std::move(e));
  }
  b["cancel"] = std::move(cancel);

  json::ValueBuilder probes(Type::kArray);
  for (const auto& p : reply.probe) {
    json::ValueBuilder e;
    e["name"] = p.item.name;
    if (p.item.variant.empty()) {
      e["damage"] = p.item.damage;
    } else {
      e["fluid"] = p.item.variant;
      e["label"] = p.label;
    }
    probes.PushBack(std::move(e));
  }
  b["probe"] = std::move(probes);

  if (reply.scan) {
    const auto& p = reply.scan->params;
    json::ValueBuilder s;
    s["id"] = reply.scan->id;
    s["threshold"] = p.threshold;
    s["maxId"] = p.maxId;
    s["chunk"] = p.chunk;
    s["slow"]["from"] = p.slowFrom;
    s["slow"]["to"] = p.slowTo;
    s["slow"]["chunk"] = p.slowChunk;
    b["scan"] = std::move(s);
  }
  return toLua(b.ExtractValue());
}

}  // namespace factoryctl::agent
