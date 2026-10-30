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

auto tick(std::vector<OrderState>& orders, const TickInput& in,
          const Settings& settings) -> TickOutput {
  TickOutput out;

  for (const auto& r : in.report.results) {
    const auto it = std::ranges::find(orders, r.order, &OrderState::id);
    if (it == orders.end()) {
      out.errors.push_back(
          std::format("result for order {}, which is not open", r.order));
      continue;
    }
    if (auto ok = applyResult(*it, r.status, r.message, in.now); !ok) {
      out.errors.push_back(ok.error());
    }
  }

  for (auto& o : orders) {
    applyCpus(o, in.report.cpus, in.now);
    if (const auto alarm = checkTimeouts(o, settings.timeouts, in.now)) {
      out.alarms.push_back({o.id, *alarm});
    }
    if (markSent(o, in.now)) out.send.push_back(o.id);
    if (needsCancelTask(o)) out.cancel.push_back({o.id, o.cpu});
  }

  std::vector<Order> open;
  auto settling = in.settling;
  for (const auto& o : orders) {
    open.push_back({o.item, o.status});
    // Closed in this tick: what it made is in no scan yet.
    if (!isOpen(o.status)) settling.push_back(o.item);
  }
  out.created = plan({in.targets, in.stock, open, in.report.cpus,
                      settings.lowFreeCpus, settling});
  return out;
}

}  // namespace factoryctl::domain
