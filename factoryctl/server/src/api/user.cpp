#include "api/user.hpp"

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/http/http_request.hpp>

namespace factoryctl::api {

namespace {

constexpr std::size_t kMaxBytes = 100;

auto continuation(char c) -> bool {
  return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U;
}

}  // namespace

auto cleanUser(std::string_view header) -> std::string {
  std::string out;
  for (const char c : header) {
    const auto u = static_cast<unsigned char>(c);
    if (u >= 0x20 && u != 0x7F) out += c;
  }
  const auto first = out.find_first_not_of(' ');
  if (first == std::string::npos) return {};
  out = out.substr(first, out.find_last_not_of(' ') - first + 1);
  if (out.size() > kMaxBytes) {
    auto cut = kMaxBytes;
    while (cut > 0 && continuation(out[cut])) --cut;
    out.resize(cut);
  }
  return out;
}

auto userOf(const userver::server::http::HttpRequest& request) -> std::string {
  return cleanUser(request.GetHeader(kUserHeader));
}

auto MeHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext&) const -> std::string {
  request.GetHttpResponse().SetContentType("application/json");
  const auto user = userOf(request);
  userver::formats::json::ValueBuilder body;
  if (user.empty()) {
    body["user"] = userver::formats::json::ValueBuilder(
        userver::formats::common::Type::kNull);
  } else {
    body["user"] = user;
  }
  return userver::formats::json::ToString(body.ExtractValue());
}

}  // namespace factoryctl::api
