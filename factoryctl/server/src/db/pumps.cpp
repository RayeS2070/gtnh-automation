#include "db/pumps.hpp"

#include <format>
#include <map>
#include <set>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/io/chrono.hpp>
#include <userver/storages/postgres/io/optional.hpp>
#include <userver/storages/postgres/transaction.hpp>

namespace factoryctl::db {

namespace {

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr std::string_view kSource = "pumps";

// A fluid of the stock, the key of goods: see domain::keyOf.
auto fluidKey(const std::string& fluid) -> domain::ItemKey {
  return {"ae2fc:fluid_drop", 0, fluid};
}

auto shortAddress(const std::string& a) -> std::string { return a.substr(0, 8); }

// One more version of the settings; made with the defaults when none.
auto bump(pg::Transaction& tx, domain::TimePoint now) -> std::int64_t {
  const domain::PumpSettings d;
  return tx
      .Execute(
          "INSERT INTO factoryctl.pump_settings (version, batch, threshold,"
          "  updated_at) VALUES (1, $1, $2, $3) "
          "ON CONFLICT (one) DO UPDATE SET"
          "  version = factoryctl.pump_settings.version + 1, updated_at = $3 "
          "RETURNING version",
          d.batch, d.threshold, pg::TimePointTz{now})
      .AsSingleRow<std::int64_t>();
}

auto readTargets(pg::Transaction& tx) -> std::vector<domain::PumpTarget> {
  std::vector<domain::PumpTarget> out;
  for (const auto& row :
       tx.Execute("SELECT fluid, target, priority, enabled "
                  "FROM factoryctl.pump_targets ORDER BY fluid")) {
    out.push_back({row["fluid"].As<std::string>(),
                   row["target"].As<std::int64_t>(),
                   row["priority"].As<double>(), row["enabled"].As<bool>()});
  }
  return out;
}

}  // namespace

auto describe(const domain::PumpTarget& t) -> std::string {
  auto text = std::format("keep {}, priority {}", t.target, t.priority);
  if (!t.enabled) text += ", paused";
  return text;
}

auto loadPumpSettings(pg::Transaction& tx)
    -> std::optional<domain::PumpSettings> {
  const auto res = tx.Execute(
      "SELECT version, batch, threshold FROM factoryctl.pump_settings "
      "FOR UPDATE");
  if (res.IsEmpty()) return std::nullopt;
  const auto row = res[0];
  domain::PumpSettings s{.version = row["version"].As<std::int64_t>(),
                         .batch = row["batch"].As<std::int32_t>(),
                         .threshold = row["threshold"].As<double>()};
  s.targets = readTargets(tx);
  return s;
}

auto storePumpSettings(pg::Transaction& tx, const domain::PumpSettings& s,
                       domain::TimePoint now) -> void {
  tx.Execute(
      "INSERT INTO factoryctl.pump_settings (version, batch, threshold,"
      "  updated_at) VALUES ($1, $2, $3, $4) "
      "ON CONFLICT (one) DO UPDATE SET version = $1, batch = $2,"
      "  threshold = $3, updated_at = $4",
      s.version, s.batch, s.threshold, pg::TimePointTz{now});
  tx.Execute("DELETE FROM factoryctl.pump_targets");
  for (const auto& t : s.targets) {
    tx.Execute(
        "INSERT INTO factoryctl.pump_targets (fluid, target, priority, enabled)"
        " VALUES ($1, $2, $3, $4)",
        t.fluid, t.target, t.priority, t.enabled);
  }
}

auto storePumpsReport(pg::Transaction& tx, const domain::PumpReport& r,
                      domain::TimePoint now) -> std::vector<Event> {
  std::vector<Event> events;
  if (r.recipes) {
    for (const auto& x : *r.recipes) {
      tx.Execute(
          "INSERT INTO factoryctl.pump_recipes (fluid, planet, gas, label, rate)"
          " VALUES ($1, $2, $3, $4, $5) ON CONFLICT (fluid) DO UPDATE SET"
          "  planet = EXCLUDED.planet, gas = EXCLUDED.gas,"
          "  label = EXCLUDED.label, rate = EXCLUDED.rate",
          x.fluid, x.planet, x.gas, x.label, x.rate);
    }
  }

  std::set<std::string> before;
  for (const auto& row :
       tx.Execute("DELETE FROM factoryctl.pump_modules RETURNING address")) {
    before.insert(row["address"].As<std::string>());
  }
  std::set<std::string> seen;
  for (const auto& m : r.modules) {
    std::vector<std::string> fluids;
    std::vector<std::int32_t> parallels;
    for (const auto& s : m.slots) {
      fluids.push_back(s.fluid);
      parallels.push_back(s.parallel);
    }
    tx.Execute(
        "INSERT INTO factoryctl.pump_modules (address, tier, state, fluids,"
        "  parallels, progress, max_progress, seen_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) ON CONFLICT DO NOTHING",
        m.address, m.tier, m.state, fluids, parallels, m.progress,
        m.maxProgress, pg::TimePointTz{now});
    seen.insert(m.address);
    if (!before.contains(m.address)) {
      events.push_back(
          {.at = now,
           .source = std::string{kSource},
           .text = std::format("pump module T{} {} appeared", m.tier,
                               shortAddress(m.address))});
    }
  }
  // An empty report says nothing of modules: an agent that just started may
  // not have looked yet.
  if (!r.modules.empty()) {
    for (const auto& a : before) {
      if (!seen.contains(a)) {
        events.push_back(
            {.at = now,
             .level = "warning",
             .source = std::string{kSource},
             .text = std::format("pump module {} is gone", shortAddress(a))});
      }
    }
  }

  for (const auto& c : r.cycles) {
    const auto inserted = tx.Execute(
        "INSERT INTO factoryctl.pump_cycles (run, seq, module, batch, result,"
        "  message, duration, ended_at) VALUES ($1, $2, $3, $4, $5, $6, $7, $8)"
        " ON CONFLICT (run, seq) DO NOTHING RETURNING seq",
        c.run, c.seq, c.module, c.batch, c.result, c.message, c.duration,
        pg::TimePointTz{now});
    if (inserted.IsEmpty()) continue;  // a poll whose reply was lost
    for (std::size_t i = 0; i < c.slots.size(); ++i) {
      const auto& s = c.slots[i];
      tx.Execute(
          "INSERT INTO factoryctl.pump_cycle_slots (run, seq, slot, fluid,"
          "  parallel, expected) VALUES ($1, $2, $3, $4, $5, $6)",
          c.run, c.seq, static_cast<std::int32_t>(i), s.fluid, s.parallel,
          s.expected);
    }
  }

  for (const auto& a : r.stock) {
    tx.Execute(
        "INSERT INTO factoryctl.pump_stock (fluid, amount, at) VALUES ($1, $2,"
        " $3) ON CONFLICT (fluid) DO UPDATE SET amount = $2, at = $3",
        a.fluid, a.amount, pg::TimePointTz{now});
  }
  return events;
}

auto pumpsView(pg::Cluster& db, domain::TimePoint since) -> PumpsView {
  PumpsView v;
  // One snapshot: modules and fluids of the same moment.
  auto tx = db.Begin(kMaster, pg::TransactionOptions{
                                  pg::IsolationLevel::kRepeatableRead,
                                  pg::TransactionOptions::kReadOnly});
  const auto settings = tx.Execute(
      "SELECT version, batch, threshold FROM factoryctl.pump_settings");
  if (!settings.IsEmpty()) {
    const auto row = settings[0];
    v.settings = {.version = row["version"].As<std::int64_t>(),
                  .batch = row["batch"].As<std::int32_t>(),
                  .threshold = row["threshold"].As<double>()};
  }

  std::map<std::string, std::int64_t> rates;
  for (const auto& row :
       tx.Execute("SELECT fluid, rate FROM factoryctl.pump_recipes")) {
    rates[row["fluid"].As<std::string>()] = row["rate"].As<std::int64_t>();
  }
  std::map<std::string, std::int64_t> inFlight;
  for (const auto& row : tx.Execute(
           "SELECT address, tier, state, fluids, parallels, progress,"
           "  max_progress, seen_at FROM factoryctl.pump_modules "
           "ORDER BY tier DESC, address")) {
    PumpModuleRow m;
    m.module.address = row["address"].As<std::string>();
    m.module.tier = row["tier"].As<std::int32_t>();
    m.module.state = row["state"].As<std::string>();
    const auto fluids = row["fluids"].As<std::vector<std::string>>();
    const auto parallels = row["parallels"].As<std::vector<std::int32_t>>();
    for (std::size_t i = 0; i < fluids.size() && i < parallels.size(); ++i) {
      m.module.slots.push_back({fluids[i], parallels[i], 0});
      if (m.module.state != "idle" && m.module.state != "backoff" &&
          v.settings) {
        const auto it = rates.find(fluids[i]);
        if (it != rates.end()) {
          inFlight[fluids[i]] += it->second * parallels[i] * v.settings->batch;
        }
      }
    }
    m.module.progress = row["progress"].As<std::optional<std::int64_t>>();
    m.module.maxProgress = row["max_progress"].As<std::optional<std::int64_t>>();
    m.seenAt = row["seen_at"].As<pg::TimePointTz>().GetUnderlying();
    v.modules.push_back(std::move(m));
  }

  for (const auto& row : tx.Execute(
           "SELECT r.fluid, r.planet, r.gas, r.label, r.rate,"
           "  t.target, t.priority, t.enabled, s.amount, g.id AS goods_id,"
           "  coalesce(p.produced, 0)::bigint AS produced "
           "FROM factoryctl.pump_recipes r "
           "LEFT JOIN factoryctl.pump_targets t ON t.fluid = r.fluid "
           "LEFT JOIN factoryctl.pump_stock s ON s.fluid = r.fluid "
           "LEFT JOIN factoryctl.goods g ON g.kind = 'item'"
           "  AND g.name = 'ae2fc:fluid_drop' AND g.damage = 0"
           "  AND g.variant = r.fluid "
           "LEFT JOIN (SELECT cs.fluid, sum(cs.expected) AS produced"
           "           FROM factoryctl.pump_cycle_slots cs"
           "           JOIN factoryctl.pump_cycles c USING (run, seq)"
           "           WHERE c.result = 'done' AND c.ended_at >= $1"
           "           GROUP BY cs.fluid) p ON p.fluid = r.fluid "
           "ORDER BY r.planet, r.gas",
           pg::TimePointTz{since})) {
    PumpFluidRow f;
    f.recipe = {row["planet"].As<std::int32_t>(), row["gas"].As<std::int32_t>(),
                row["fluid"].As<std::string>(), row["label"].As<std::string>(),
                row["rate"].As<std::int64_t>()};
    if (const auto target = row["target"].As<std::optional<std::int64_t>>()) {
      f.target = domain::PumpTarget{f.recipe.fluid, *target,
                                    row["priority"].As<double>(),
                                    row["enabled"].As<bool>()};
    }
    f.amount = row["amount"].As<std::optional<std::int64_t>>();
    f.goodsId = row["goods_id"].As<std::optional<std::int64_t>>();
    f.produced = row["produced"].As<std::int64_t>();
    f.inFlight = inFlight[f.recipe.fluid];
    v.fluids.push_back(std::move(f));
  }
  v.failed = tx.Execute(
                   "SELECT count(*) FROM factoryctl.pump_cycles "
                   "WHERE result = 'failed' AND ended_at >= $1",
                   pg::TimePointTz{since})
                 .AsSingleRow<std::int64_t>();
  tx.Commit();
  return v;
}

auto setPumpGeneral(pg::Cluster& db, std::int32_t batch, double threshold,
                    domain::TimePoint now)
    -> std::expected<std::int64_t, std::string> {
  if (auto ok = domain::checkPumpSettings({.batch = batch, .threshold = threshold});
      !ok) {
    return std::unexpected(ok.error());
  }
  auto tx = db.Begin(kMaster, {});
  const auto version = bump(tx, now);
  tx.Execute(
      "UPDATE factoryctl.pump_settings SET batch = $1, threshold = $2",
      batch, threshold);
  const Event told{
      .at = now,
      .source = std::string{kSource},
      .text = std::format("pumping: batch {} s, threshold {}", batch, threshold)};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return version;
}

auto setPumpTarget(pg::Cluster& db, const domain::PumpTarget& t,
                   domain::TimePoint now)
    -> std::expected<std::int64_t, std::string> {
  if (auto ok = domain::checkPumpSettings({.targets = {t}}); !ok) {
    return std::unexpected(ok.error());
  }
  auto tx = db.Begin(kMaster, {});
  if (tx.Execute("SELECT 1 FROM factoryctl.pump_recipes WHERE fluid = $1",
                 t.fluid)
          .IsEmpty()) {
    return std::unexpected(std::format("no pump module pumps {}", t.fluid));
  }
  const auto version = bump(tx, now);
  tx.Execute(
      "INSERT INTO factoryctl.pump_targets (fluid, target, priority, enabled)"
      " VALUES ($1, $2, $3, $4) ON CONFLICT (fluid) DO UPDATE SET"
      "  target = $2, priority = $3, enabled = $4",
      t.fluid, t.target, t.priority, t.enabled);
  const Event told{.at = now,
                   .source = std::string{kSource},
                   .text = "pumping target: " + describe(t),
                   .goods = fluidKey(t.fluid)};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return version;
}

auto deletePumpTarget(pg::Cluster& db, const std::string& fluid,
                      domain::TimePoint now) -> std::optional<std::int64_t> {
  auto tx = db.Begin(kMaster, {});
  if (tx.Execute("DELETE FROM factoryctl.pump_targets WHERE fluid = $1", fluid)
          .RowsAffected() == 0) {
    return std::nullopt;
  }
  const auto version = bump(tx, now);
  const Event told{.at = now,
                   .source = std::string{kSource},
                   .text = "pumping target removed",
                   .goods = fluidKey(fluid)};
  addEvents(tx, {&told, 1});
  tx.Commit();
  return version;
}

auto deletePumpCyclesBefore(pg::Cluster& db, domain::TimePoint before)
    -> std::int64_t {
  return static_cast<std::int64_t>(
      db.Execute(kMaster, "DELETE FROM factoryctl.pump_cycles WHERE ended_at < $1",
                 pg::TimePointTz{before})
          .RowsAffected());
}

}  // namespace factoryctl::db
