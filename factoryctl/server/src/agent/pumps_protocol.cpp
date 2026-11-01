#include "agent/pumps_protocol.hpp"

#include <format>
#include <userver/formats/json/value_builder.hpp>

#include "agent/fields.hpp"
#include "agent/lua.hpp"

namespace factoryctl::agent {

namespace {

namespace json = userver::formats::json;
using namespace fields;

auto number(const json::Value& v, std::string_view path) -> Result<double> {
  if (v.IsMissing()) return fail(path, "missing");
  if (!v.IsDouble()) return fail(path, "not a number");
  return v.As<double>();
}

auto int32(const json::Value& v, std::string_view path)
    -> Result<std::int32_t> {
  const auto n = integer(v, path);
  if (!n) return std::unexpected(n.error());
  if (*n < INT32_MIN || *n > INT32_MAX) return fail(path, "out of range");
  return static_cast<std::int32_t>(*n);
}

template <typename T, typename F>
auto list(const json::Value& v, std::string_view path, F one)
    -> Result<std::vector<T>> {
  std::vector<T> out;
  if (v.IsMissing()) return out;
  if (!v.IsArray()) return fail(path, "not an array");
  out.reserve(v.GetSize());
  for (std::size_t i = 0; i < v.GetSize(); ++i) {
    auto e = one(v[i], std::format("{}[{}]", path, i));
    if (!e) return std::unexpected(e.error());
    out.push_back(*std::move(e));
  }
  return out;
}

auto slot(const json::Value& v, std::string_view path, bool withExpected)
    -> Result<domain::PumpSlot> {
  if (!v.IsObject()) return fail(path, "not an object");
  domain::PumpSlot s;
  const auto fluid = text(v["fluid"], std::format("{}.fluid", path));
  if (!fluid) return std::unexpected(fluid.error());
  s.fluid = *fluid;
  const auto parallel = int32(v["parallel"], std::format("{}.parallel", path));
  if (!parallel) return std::unexpected(parallel.error());
  s.parallel = *parallel;
  if (withExpected) {
    const auto e = integer(v["expected"], std::format("{}.expected", path));
    if (!e) return std::unexpected(e.error());
    s.expected = *e;
  }
  return s;
}

auto target(const json::Value& v, std::string_view path)
    -> Result<domain::PumpTarget> {
  if (!v.IsObject()) return fail(path, "not an object");
  domain::PumpTarget t;
  const auto fluid = text(v["fluid"], std::format("{}.fluid", path));
  if (!fluid) return std::unexpected(fluid.error());
  t.fluid = *fluid;
  const auto amount = integer(v["target"], std::format("{}.target", path));
  if (!amount) return std::unexpected(amount.error());
  t.target = *amount;
  const auto priority = number(v["priority"], std::format("{}.priority", path));
  if (!priority) return std::unexpected(priority.error());
  t.priority = *priority;
  const auto enabled = flag(v["enabled"], std::format("{}.enabled", path), true);
  if (!enabled) return std::unexpected(enabled.error());
  t.enabled = *enabled;
  return t;
}

auto settings(const json::Value& v, std::string_view path)
    -> Result<domain::PumpSettings> {
  if (!v.IsObject()) return fail(path, "not an object");
  domain::PumpSettings s;
  if (!v["version"].IsMissing()) {
    const auto version = integer(v["version"], std::format("{}.version", path));
    if (!version) return std::unexpected(version.error());
    s.version = *version;
  }
  const auto batch = int32(v["batch"], std::format("{}.batch", path));
  if (!batch) return std::unexpected(batch.error());
  s.batch = *batch;
  const auto threshold =
      number(v["threshold"], std::format("{}.threshold", path));
  if (!threshold) return std::unexpected(threshold.error());
  s.threshold = *threshold;
  auto targets = list<domain::PumpTarget>(
      v["targets"], std::format("{}.targets", path), target);
  if (!targets) return std::unexpected(targets.error());
  s.targets = *std::move(targets);
  if (auto ok = domain::checkPumpSettings(s); !ok) return fail(path, ok.error());
  return s;
}

auto recipe(const json::Value& v, std::string_view path)
    -> Result<domain::PumpRecipe> {
  if (!v.IsObject()) return fail(path, "not an object");
  domain::PumpRecipe r;
  const auto planet = int32(v["planet"], std::format("{}.planet", path));
  if (!planet) return std::unexpected(planet.error());
  r.planet = *planet;
  const auto gas = int32(v["gas"], std::format("{}.gas", path));
  if (!gas) return std::unexpected(gas.error());
  r.gas = *gas;
  const auto fluid = text(v["fluid"], std::format("{}.fluid", path));
  if (!fluid) return std::unexpected(fluid.error());
  r.fluid = *fluid;
  const auto label = text(v["label"], std::format("{}.label", path));
  if (!label) return std::unexpected(label.error());
  r.label = *label;
  const auto rate = integer(v["rate"], std::format("{}.rate", path));
  if (!rate) return std::unexpected(rate.error());
  r.rate = *rate;
  return r;
}

auto pumpModule(const json::Value& v, std::string_view path)
    -> Result<domain::PumpModule> {
  if (!v.IsObject()) return fail(path, "not an object");
  domain::PumpModule m;
  const auto address = text(v["address"], std::format("{}.address", path));
  if (!address) return std::unexpected(address.error());
  m.address = *address;
  const auto tier = int32(v["tier"], std::format("{}.tier", path));
  if (!tier) return std::unexpected(tier.error());
  m.tier = *tier;
  const auto state = text(v["state"], std::format("{}.state", path));
  if (!state) return std::unexpected(state.error());
  m.state = *state;
  auto slots = list<domain::PumpSlot>(
      v["slots"], std::format("{}.slots", path),
      [](const json::Value& e, std::string_view p) { return slot(e, p, false); });
  if (!slots) return std::unexpected(slots.error());
  m.slots = *std::move(slots);
  for (const auto& [field, out] : {std::pair{"progress", &m.progress},
                                   std::pair{"maxProgress", &m.maxProgress}}) {
    if (v[field].IsMissing()) continue;
    const auto n = integer(v[field], std::format("{}.{}", path, field));
    if (!n) return std::unexpected(n.error());
    *out = *n;
  }
  return m;
}

auto cycle(const json::Value& v, std::string_view path)
    -> Result<domain::PumpCycle> {
  if (!v.IsObject()) return fail(path, "not an object");
  domain::PumpCycle c;
  const auto run = text(v["run"], std::format("{}.run", path));
  if (!run) return std::unexpected(run.error());
  c.run = *run;
  const auto seq = integer(v["seq"], std::format("{}.seq", path));
  if (!seq) return std::unexpected(seq.error());
  c.seq = *seq;
  const auto module = text(v["module"], std::format("{}.module", path));
  if (!module) return std::unexpected(module.error());
  c.module = *module;
  const auto batch = int32(v["batch"], std::format("{}.batch", path));
  if (!batch) return std::unexpected(batch.error());
  c.batch = *batch;
  const auto result = text(v["result"], std::format("{}.result", path));
  if (!result) return std::unexpected(result.error());
  if (*result != "done" && *result != "failed") {
    return fail(std::format("{}.result", path),
                std::format("unknown '{}'", *result));
  }
  c.result = *result;
  if (!v["message"].IsMissing()) {
    const auto m = text(v["message"], std::format("{}.message", path));
    if (!m) return std::unexpected(m.error());
    c.message = *m;
  }
  if (!v["duration"].IsMissing()) {
    const auto d = number(v["duration"], std::format("{}.duration", path));
    if (!d) return std::unexpected(d.error());
    c.duration = *d;
  }
  auto slots = list<domain::PumpSlot>(
      v["slots"], std::format("{}.slots", path),
      [](const json::Value& e, std::string_view p) { return slot(e, p, true); });
  if (!slots) return std::unexpected(slots.error());
  c.slots = *std::move(slots);
  return c;
}

auto amount(const json::Value& v, std::string_view path)
    -> Result<domain::FluidAmount> {
  if (!v.IsObject()) return fail(path, "not an object");
  domain::FluidAmount a;
  const auto fluid = text(v["fluid"], std::format("{}.fluid", path));
  if (!fluid) return std::unexpected(fluid.error());
  a.fluid = *fluid;
  const auto n = integer(v["amount"], std::format("{}.amount", path));
  if (!n) return std::unexpected(n.error());
  a.amount = *n;
  return a;
}

}  // namespace

auto parsePumpsPoll(const json::Value& body) -> Result<domain::PumpReport> {
  if (!body.IsObject()) return fail("pumps poll", "not an object");
  domain::PumpReport r;
  const auto run = text(body["run"], "run");
  if (!run) return std::unexpected(run.error());
  r.run = *run;
  const auto version = integer(body["settings"], "settings");
  if (!version) return std::unexpected(version.error());
  r.settings = *version;
  if (!body["offer"].IsMissing()) {
    auto offer = settings(body["offer"], "offer");
    if (!offer) return std::unexpected(offer.error());
    r.offer = *std::move(offer);
  }
  if (!body["recipes"].IsMissing()) {
    auto recipes = list<domain::PumpRecipe>(body["recipes"], "recipes", recipe);
    if (!recipes) return std::unexpected(recipes.error());
    r.recipes = *std::move(recipes);
  }
  auto modules = list<domain::PumpModule>(body["modules"], "modules", pumpModule);
  if (!modules) return std::unexpected(modules.error());
  r.modules = *std::move(modules);
  auto cycles = list<domain::PumpCycle>(body["cycles"], "cycles", cycle);
  if (!cycles) return std::unexpected(cycles.error());
  r.cycles = *std::move(cycles);
  auto stock = list<domain::FluidAmount>(body["stock"], "stock", amount);
  if (!stock) return std::unexpected(stock.error());
  r.stock = *std::move(stock);
  auto log = list<domain::AgentLogLine>(body["log"], "log", logLine);
  if (!log) return std::unexpected(log.error());
  r.log = *std::move(log);
  return r;
}

auto replyPumpsPoll(const PumpsReply& reply) -> Result<std::string> {
  using userver::formats::common::Type;
  json::ValueBuilder b;
  b["poll"] = reply.poll.count();
  if (reply.settings) {
    const auto& s = *reply.settings;
    json::ValueBuilder e;
    e["version"] = s.version;
    e["batch"] = s.batch;
    e["threshold"] = s.threshold;
    json::ValueBuilder targets(Type::kArray);
    for (const auto& t : s.targets) {
      json::ValueBuilder one;
      one["fluid"] = t.fluid;
      one["target"] = t.target;
      one["priority"] = t.priority;
      one["enabled"] = t.enabled;
      targets.PushBack(std::move(one));
    }
    e["targets"] = std::move(targets);
    b["settings"] = std::move(e);
  }
  if (reply.offer) b["offer"] = true;
  return toLua(b.ExtractValue());
}

}  // namespace factoryctl::agent
