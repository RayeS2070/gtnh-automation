#include "config/dynamic.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <ranges>
#include <stdexcept>
#include <userver/formats/common/items.hpp>
#include <userver/formats/yaml/serialize.hpp>
#include <userver/formats/yaml/value.hpp>
#include <userver/utils/string_to_duration.hpp>

namespace factoryctl::config {

namespace {

namespace json = userver::formats::json;
using std::chrono::seconds;

[[noreturn]] auto fail(std::string_view key, std::string_view field,
                       std::string_view why) -> void {
  throw std::runtime_error(std::format("{}.{}: {}", key, field, why));
}

// A period like 10s or 5m, at least a second: 0 or 500ms is a typo.
auto period(const json::Value& v, std::string_view key, std::string_view field)
    -> seconds {
  constexpr std::string_view kWhat = "a period like 10s, 5m, 1h";
  if (!v.IsString()) fail(key, field, kWhat);
  std::chrono::milliseconds ms{};
  try {
    ms = userver::utils::StringToDuration(v.As<std::string>());
  } catch (const std::exception&) {
    fail(key, field, kWhat);
  }
  if (ms < seconds{1}) fail(key, field, "at least 1s");
  return std::chrono::duration_cast<seconds>(ms);
}

auto integer(const json::Value& v, std::string_view key, std::string_view field,
             std::int64_t min) -> std::int64_t {
  if (!v.IsInt64()) fail(key, field, "a whole number");
  const auto n = v.As<std::int64_t>();
  if (n < min) fail(key, field, std::format("at least {}", min));
  return n;
}

auto int32(const json::Value& v, std::string_view key, std::string_view field,
           std::int32_t min) -> std::int32_t {
  const auto n = integer(v, key, field, min);
  if (n > std::numeric_limits<std::int32_t>::max()) {
    fail(key, field, "too large");
  }
  return static_cast<std::int32_t>(n);
}

template <typename T>
using Fields =
    std::map<std::string_view,
             std::function<void(T&, const json::Value&, std::string_view)>,
             std::less<>>;

// Fills `out` from the members of `value`: each must be one of `fields`.
template <typename T>
auto fill(T& out, const json::Value& value, std::string_view key,
          const Fields<T>& fields) -> void {
  if (value.IsNull()) return;
  if (!value.IsObject()) {
    throw std::runtime_error(std::format("{}: an object of fields", key));
  }
  for (const auto& [name, item] : userver::formats::common::Items(value)) {
    const auto it = fields.find(name);
    if (it == fields.end()) fail(key, name, "no such field");
    it->second(out, item, name);
  }
}

constexpr std::string_view kPollName = "FACTORYCTL_POLL";
constexpr std::string_view kEventsName = "FACTORYCTL_EVENTS";

auto periodOf(seconds domain::Settings::* member) {
  return [member](domain::Settings& s, const json::Value& v,
                  std::string_view field) {
    s.*member = period(v, kPollName, field);
  };
}

auto timeoutOf(seconds domain::Timeouts::* member) {
  return [member](domain::Settings& s, const json::Value& v,
                  std::string_view field) {
    s.timeouts.*member = period(v, kPollName, field);
  };
}

auto scanOf(std::int32_t domain::ScanParams::* member, std::int32_t min) {
  return [member, min](domain::Settings& s, const json::Value& v,
                       std::string_view field) {
    s.scan.*member = int32(v, kPollName, field, min);
  };
}

auto pollFields() -> const Fields<domain::Settings>& {
  static const Fields<domain::Settings> fields{
      {"poll-interval", periodOf(&domain::Settings::poll)},
      {"scan-every", periodOf(&domain::Settings::scanEvery)},
      {"scan-give-up", periodOf(&domain::Settings::scanGiveUp)},
      {"sent-timeout", timeoutOf(&domain::Timeouts::sent)},
      {"computing-timeout", timeoutOf(&domain::Timeouts::computing)},
      {"stalled-timeout", timeoutOf(&domain::Timeouts::stalled)},
      {"low-free-cpus",
       [](domain::Settings& s, const json::Value& v, std::string_view field) {
         s.lowFreeCpus = int32(v, kPollName, field, 0);
       }},
      {"scan-threshold",
       [](domain::Settings& s, const json::Value& v, std::string_view field) {
         s.scan.threshold = integer(v, kPollName, field, 0);
       }},
      {"scan-max-id", scanOf(&domain::ScanParams::maxId, 1)},
      {"scan-chunk", scanOf(&domain::ScanParams::chunk, 1)},
      {"scan-slow-from", scanOf(&domain::ScanParams::slowFrom, 0)},
      {"scan-slow-to", scanOf(&domain::ScanParams::slowTo, 0)},
      {"scan-slow-chunk", scanOf(&domain::ScanParams::slowChunk, 1)},
  };
  return fields;
}

auto eventsFields() -> const Fields<EventsSettings>& {
  static const Fields<EventsSettings> fields{
      {"keep",
       [](EventsSettings& e, const json::Value& v, std::string_view field) {
         e.keep = period(v, kEventsName, field);
       }},
  };
  return fields;
}

}  // namespace

auto parsePoll(const json::Value& value) -> domain::Settings {
  domain::Settings s;
  fill(s, value, kPollName, pollFields());
  if (s.scan.slowFrom > s.scan.slowTo) {
    fail(kPollName, "scan-slow-from", "after scan-slow-to");
  }
  return s;
}

auto parseEvents(const json::Value& value) -> EventsSettings {
  EventsSettings e;
  fill(e, value, kEventsName, eventsFields());
  return e;
}

const userver::dynamic_config::Key<domain::Settings> kPoll{
    kPollName, parsePoll, userver::dynamic_config::DefaultAsJsonString{"{}"}};

const userver::dynamic_config::Key<EventsSettings> kEvents{
    kEventsName, parseEvents,
    userver::dynamic_config::DefaultAsJsonString{"{}"}};

auto parseDocs(std::string_view yaml) -> userver::dynamic_config::DocsMap {
  userver::dynamic_config::DocsMap docs;
  // userver throws on a document without nodes: a file of comments is that.
  const auto blank = [](std::string_view line) {
    const auto at = line.find_first_not_of(" \t\r");
    return at == std::string_view::npos || line[at] == '#';
  };
  if (std::ranges::all_of(std::views::split(yaml, '\n'), [&](auto&& line) {
        return blank(std::string_view{line});
      })) {
    return docs;
  }
  const auto root = userver::formats::yaml::FromString(std::string{yaml});
  if (root.IsMissing() || root.IsNull()) return docs;
  if (!root.IsObject()) {
    throw std::runtime_error("not a mapping of keys (FACTORYCTL_POLL: ...)");
  }
  for (const auto& [name, value] : userver::formats::common::Items(root)) {
    docs.Set(name, value.As<json::Value>());
  }
  return docs;
}

auto keyOfField(std::string_view field) -> std::optional<std::string_view> {
  if (pollFields().contains(field)) return kPollName;
  if (eventsFields().contains(field)) return kEventsName;
  return std::nullopt;
}

auto unknownNames(const userver::dynamic_config::DocsMap& docs,
                  const std::unordered_set<std::string>& known)
    -> std::vector<std::string> {
  std::vector<std::string> unknown;
  for (const auto& name : docs.GetNames()) {
    if (!known.contains(name)) unknown.push_back(name);
  }
  std::ranges::sort(unknown);
  return unknown;
}

}  // namespace factoryctl::config
