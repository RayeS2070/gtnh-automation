#include "domain/poll.hpp"

#include <algorithm>
#include <format>

namespace factoryctl::domain {

auto needScan(const ScanState& scans, const Settings& settings, TimePoint now)
    -> bool {
  if (scans.running && now - *scans.running <= settings.scanGiveUp) {
    return false;
  }
  return !scans.lastFinished || now - *scans.lastFinished >= settings.scanEvery;
}

auto trustedAmount(const Probe& probe, std::int64_t scanned)
    -> std::optional<std::int64_t> {
  if (probe.size) return probe.size;
  if (scanned == 0) return 0;
  return std::nullopt;
}

auto tick(std::vector<OrderState>& orders, const TickInput& in,
          const Settings& settings) -> TickOutput {
  TickOutput out;
  // Whether AE can craft: the database's view, then what this report tells.
  auto uncraftable = in.uncraftable;
  const auto learn = [&](const ItemKey& item, bool craftable) {
    out.craftability.push_back({item, craftable});
    if (craftable) {
      std::erase(uncraftable, item);
    } else if (!std::ranges::contains(uncraftable, item)) {
      uncraftable.push_back(item);
    }
  };

  // Records a step if it changed the status.
  const auto told = [&](const OrderState& o, OrderStatus before) {
    if (o.status != before) {
      out.changes.push_back({o.id, before, o.status, describe(o)});
    }
  };

  for (const auto& r : in.report.results) {
    const auto it = std::ranges::find(orders, r.order, &OrderState::id);
    if (it == orders.end()) {
      out.errors.push_back(
          std::format("result for order {}, which is not open", r.order));
      continue;
    }
    const auto before = it->status;
    if (auto ok = applyResult(*it, r.status, r.message, in.now); !ok) {
      out.errors.push_back(ok.error());
    }
    told(*it, before);
    if (r.craftable) learn(it->item, *r.craftable);
  }
  // Probes last: taken after the CPUs of this step, while a result may come
  // from a request made a step before.
  for (const auto& p : in.report.stock) {
    if (p.craftable) learn(p.item, *p.craftable);
  }

  for (auto& o : orders) {
    auto before = o.status;
    applyCpus(o, in.report.cpus, in.now);
    told(o, before);
    before = o.status;
    if (const auto alarm = checkTimeouts(o, settings.timeouts, in.now)) {
      out.alarms.push_back({o.id, *alarm});
    }
    told(o, before);
    if (markSent(o, in.now)) {
      out.send.push_back(o.id);
      out.changes.push_back(
          {o.id, OrderStatus::kQueued, OrderStatus::kSent, describe(o)});
    }
    if (needsCancelTask(o)) out.cancel.push_back({o.id, o.cpu});
    // AE keeps such a craft on its CPU, where it may wait forever. Logged
    // when first seen; canceling it is a user's decision.
    const bool lostNow = std::ranges::contains(uncraftable, o.item) &&
                         !std::ranges::contains(in.uncraftable, o.item);
    if (lostNow && (o.status == OrderStatus::kAccepted ||
                    o.status == OrderStatus::kCrafting)) {
      out.alarms.push_back({o.id, AlarmKind::kNoLongerCraftable});
    }
  }

  std::vector<Order> open;
  auto settling = in.settling;
  for (const auto& o : orders) {
    open.push_back({o.item, o.status});
    // Closed in this tick: what it made is in no scan yet, but it is in a
    // probe of this report (taken after the CPUs were read).
    const bool probed =
        std::ranges::contains(in.report.stock, o.item, &Probe::item);
    if (!isOpen(o.status) && !probed) settling.push_back(o.item);
  }
  out.created = plan({in.targets, in.stock, open, in.report.cpus,
                      settings.lowFreeCpus, settling, uncraftable});
  return out;
}

}  // namespace factoryctl::domain
