#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/value.hpp>
#include <userver/formats/json/value.hpp>
#include <vector>

#include "domain/poll.hpp"

// The dynamic config: values changed while the server runs, read where they
// are used (Source::GetSnapshot). config::FileUpdater brings them from a YAML
// file; a key or field missing there keeps its default.
namespace factoryctl::config {

// FACTORYCTL_POLL, the agent's poll: the fields of domain::Settings, named as
// in config_vars before (poll-interval, scan-every, ...), periods like 10s.
extern const userver::dynamic_config::Key<domain::Settings> kPoll;

struct EventsSettings {
  std::chrono::seconds keep{std::chrono::days{30}};

  auto operator==(const EventsSettings&) const -> bool = default;
};

// FACTORYCTL_EVENTS: keep, how long events stay.
extern const userver::dynamic_config::Key<EventsSettings> kEvents;

struct MiningSettings {
  // Items of one ore in the buffer before processing at which mining stops
  // giving it to modules: the factory is not keeping up.
  double bufferLimit = 100'000;
  // mB of a plasma below which it does not count as available.
  double plasmaMinimum = 100'000;
  // The target made for a drone's drill tip or rod missing one (domain::
  // miningSupplies): large, so that the order is not lost in a busy queue.
  std::int64_t drillThreshold = 16'384;
  std::int64_t drillBatch = 4'096;
  // The plasma kept stocked by a target made for it (its fluid name,
  // plasma.radon); empty: none. Which one is worth crafting is the player's.
  std::string plasma;
  std::int64_t plasmaThreshold = 1'000'000;
  std::int64_t plasmaBatch = 100'000;

  auto operator==(const MiningSettings&) const -> bool = default;
};

// FACTORYCTL_MINING: buffer-limit, plasma-minimum, drill-threshold,
// drill-batch, plasma, plasma-threshold, plasma-batch.
extern const userver::dynamic_config::Key<MiningSettings> kMining;

// The parsers of the keys. A missing field keeps its default, a key without
// fields (null) is all defaults; an unknown field or a bad value throws, and
// userver refuses the whole update.
auto parsePoll(const userver::formats::json::Value& value) -> domain::Settings;
auto parseEvents(const userver::formats::json::Value& value) -> EventsSettings;
auto parseMining(const userver::formats::json::Value& value) -> MiningSettings;

// The file's text: a YAML mapping of keys; empty or only comments is none.
// Throws on bad YAML or a document that is not a mapping.
auto parseDocs(std::string_view yaml) -> userver::dynamic_config::DocsMap;

// The key that has `field`: a field written at the top of the file (its key
// line left commented out) is a mistake the log can name.
auto keyOfField(std::string_view field) -> std::optional<std::string_view>;

// Names in `docs` that no key of the server has: typos, most likely.
auto unknownNames(const userver::dynamic_config::DocsMap& docs,
                  const std::unordered_set<std::string>& known)
    -> std::vector<std::string>;

}  // namespace factoryctl::config
