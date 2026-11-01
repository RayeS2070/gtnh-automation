#include "domain/pumps.hpp"

#include <format>
#include <set>

namespace factoryctl::domain {

namespace {

constexpr std::int32_t kBatchMax = 128;  // TileEntityModulePump.PUMP_BATCH_MAX

}  // namespace

auto checkPumpSettings(const PumpSettings& s)
    -> std::expected<void, std::string> {
  if (s.batch < 1 || s.batch > kBatchMax) {
    return std::unexpected(
        std::format("batch: {} is not in 1..{}", s.batch, kBatchMax));
  }
  if (!(s.threshold > 0 && s.threshold <= 1)) {
    return std::unexpected("threshold: not in (0, 1]");
  }
  std::set<std::string> seen;
  for (const auto& t : s.targets) {
    if (t.fluid.empty()) return std::unexpected("a target without a fluid");
    if (!seen.insert(t.fluid).second) {
      return std::unexpected(std::format("{}: twice", t.fluid));
    }
    if (t.target < 0) return std::unexpected(std::format("{}: target < 0", t.fluid));
    if (!(t.priority > 0)) {
      return std::unexpected(std::format("{}: priority must be > 0", t.fluid));
    }
  }
  return {};
}

}  // namespace factoryctl::domain
