#include "agent/lua.hpp"

#include <array>
#include <charconv>
#include <format>
#include <string_view>
#include <userver/formats/json/iterator.hpp>

namespace factoryctl::agent {

namespace {

namespace json = userver::formats::json;

constexpr std::array<std::string_view, 22> kReserved = {
    "and",      "break",  "do",   "else", "elseif", "end",  "false", "for",
    "function", "goto",   "if",   "in",   "local",  "nil",  "not",   "or",
    "repeat",   "return", "then", "true", "until",  "while"};

auto isName(std::string_view key) -> bool {
  const auto letter = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  };
  const auto digit = [](char c) { return c >= '0' && c <= '9'; };
  if (key.empty() || !letter(key.front())) return false;
  for (const char c : key) {
    if (!letter(c) && !digit(c)) return false;
  }
  return std::ranges::find(kReserved, key) == kReserved.end();
}

auto appendString(std::string& out, std::string_view s) -> void {
  out += '"';
  for (std::size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      default:
        if (c < 0x20 || c == 0x7f) {
          // \ddd reads up to three digits: pad when a digit follows.
          const bool digitNext =
              i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9';
          out += digitNext ? std::format("\\{:03}", c) : std::format("\\{}", c);
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

auto appendNumber(std::string& out, const json::Value& value)
    -> std::expected<void, std::string> {
  if (value.IsInt64()) {
    out += std::to_string(value.As<std::int64_t>());
    return {};
  }
  if (value.IsUInt64()) {
    // Lua 5.3 integers are 64-bit signed; a larger literal turns into a float.
    return std::unexpected(
        std::format("{}: integer does not fit Lua", value.GetPath()));
  }
  // No inf or nan here: userver refuses to hold them in a JSON value.
  const auto d = value.As<double>();
  std::array<char, 32> buf{};
  const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), d);
  out.append(buf.data(), end);
  return {};
}

auto append(std::string& out, const json::Value& value)
    -> std::expected<void, std::string> {
  if (value.IsNull()) {
    out += "nil";
  } else if (value.IsBool()) {
    out += value.As<bool>() ? "true" : "false";
  } else if (value.IsNumber()) {
    return appendNumber(out, value);
  } else if (value.IsString()) {
    appendString(out, value.As<std::string>());
  } else if (value.IsArray()) {
    out += '{';
    bool first = true;
    for (const auto& item : value) {
      if (item.IsNull()) {
        return std::unexpected(
            std::format("{}: null in an array", item.GetPath()));
      }
      if (!first) out += ',';
      first = false;
      if (auto ok = append(out, item); !ok) return ok;
    }
    out += '}';
  } else if (value.IsObject()) {
    out += '{';
    bool first = true;
    for (auto it = value.begin(); it != value.end(); ++it) {
      if (it->IsNull()) continue;
      if (!first) out += ',';
      first = false;
      const auto key = it.GetName();
      if (isName(key)) {
        out += key;
      } else {
        out += '[';
        appendString(out, key);
        out += ']';
      }
      out += '=';
      if (auto ok = append(out, *it); !ok) return ok;
    }
    out += '}';
  } else {
    return std::unexpected(
        std::format("{}: unsupported value", value.GetPath()));
  }
  return {};
}

}  // namespace

auto toLua(const json::Value& value)
    -> std::expected<std::string, std::string> {
  std::string out;
  if (auto ok = append(out, value); !ok) {
    return std::unexpected(std::move(ok).error());
  }
  return out;
}

}  // namespace factoryctl::agent
