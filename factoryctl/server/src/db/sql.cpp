#include "db/sql.hpp"

#include <charconv>

// Registers the embedded schema.sql: include it in exactly one source file,
// one the linker keeps (schemaSql() below is used).
#include <generated/schema.sql.hpp>
#include <userver/utils/resources.hpp>

namespace factoryctl::db {

namespace {

auto trimmed(std::string_view s) -> std::string_view {
  constexpr std::string_view kSpace = " \t\r\n";
  const auto first = s.find_first_not_of(kSpace);
  if (first == std::string_view::npos) return {};
  return s.substr(first, s.find_last_not_of(kSpace) - first + 1);
}

}  // namespace

auto schemaSql() -> std::string {
  return userver::utils::FindResource("schema_sql");
}

auto splitStatements(std::string_view sql) -> std::vector<std::string> {
  std::vector<std::string> out;
  std::string current;
  const auto flush = [&] {
    if (const auto s = trimmed(current); !s.empty()) out.emplace_back(s);
    current.clear();
  };

  bool inString = false;
  for (std::size_t i = 0; i < sql.size(); ++i) {
    const char c = sql[i];
    if (inString) {
      current += c;
      // '' is a quote inside the string; a single ' closes it.
      if (c == '\'') {
        if (i + 1 < sql.size() && sql[i + 1] == '\'') {
          current += sql[++i];
        } else {
          inString = false;
        }
      }
    } else if (c == '\'') {
      inString = true;
      current += c;
    } else if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
      // A comment runs to the end of the line; the line break stays.
      while (i < sql.size() && sql[i] != '\n') ++i;
      if (i < sql.size()) current += '\n';
    } else if (c == ';') {
      flush();
    } else {
      current += c;
    }
  }
  flush();
  return out;
}

auto schemaVersion(std::string_view sql) -> std::optional<int> {
  constexpr std::string_view kPrefix =
      "INSERT INTO factoryctl.schema_version VALUES (";
  const auto at = sql.find(kPrefix);
  if (at == std::string_view::npos) return std::nullopt;
  const auto rest = sql.substr(at + kPrefix.size());
  int version = 0;
  const auto [end, ec] =
      std::from_chars(rest.data(), rest.data() + rest.size(), version);
  if (ec != std::errc{} || end == rest.data() + rest.size() || *end != ')') {
    return std::nullopt;
  }
  return version;
}

}  // namespace factoryctl::db
