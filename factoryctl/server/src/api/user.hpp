#pragma once

// Who sends a request. factoryctl keeps no accounts: the proxy in front of it
// (nginx auth_basic) checks the person and passes the name in X-Forwarded-User,
// overwriting whatever the client sent. Without the proxy (an SSH tunnel) there
// is no one: the name is empty and the client may give its own.

#include <string>
#include <string_view>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>

namespace factoryctl::api {

inline constexpr std::string_view kUserHeader = "X-Forwarded-User";

// The header's value as a name: trimmed, no control characters, at most 100
// bytes (cut on a UTF-8 character). Empty: no one.
auto cleanUser(std::string_view header) -> std::string;

auto userOf(const userver::server::http::HttpRequest& request) -> std::string;

// GET /api/v1/me: {"user": "pavel"}, or {"user": null} without the proxy.
class MeHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-api-me";

  using HttpHandlerBase::HttpHandlerBase;

  auto HandleRequestThrow(const userver::server::http::HttpRequest& request,
                          userver::server::request::RequestContext& context)
      const -> std::string override;
};

}  // namespace factoryctl::api
