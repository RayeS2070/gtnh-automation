#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/server/middlewares/http_middleware_base.hpp>

// Compression of responses: userver 3.2 decompresses requests only. The stock
// list is some 730 KB of JSON, asked for every 5 s by an open page.
namespace factoryctl::http {

enum class Encoding { kGzip, kZstd };

auto name(Encoding e) -> std::string_view;

// The encoding to answer with, from the request's Accept-Encoding: zstd
// before gzip, none for what the client does not take (or takes with q=0).
auto chooseEncoding(std::string_view acceptEncoding) -> std::optional<Encoding>;

// Text compresses ten times; images, fonts and the icon index are compressed already.
auto compressible(std::string_view contentType, std::size_t size) -> bool;

auto compress(Encoding e, std::string_view data) -> std::string;

class CompressionMiddleware final
    : public userver::server::middlewares::HttpMiddlewareBase {
 public:
  static constexpr std::string_view kName = "compression";

  explicit CompressionMiddleware(
      const userver::server::handlers::HttpHandlerBase&) {}

 private:
  void HandleRequest(
      userver::server::http::HttpRequest& request,
      userver::server::request::RequestContext& context) const override;
};

using CompressionFactory =
    userver::server::middlewares::SimpleHttpMiddlewareFactory<
        CompressionMiddleware>;

}  // namespace factoryctl::http
