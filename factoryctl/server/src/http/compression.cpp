#include "http/compression.hpp"

#include <zlib.h>
#include <zstd.h>

#include <cctype>
#include <charconv>
#include <ranges>
#include <stdexcept>
#include <userver/http/common_headers.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_response.hpp>

namespace factoryctl::http {

namespace {

namespace headers = userver::http::headers;

// Smaller bodies fit in a packet or two anyway.
constexpr std::size_t kMinSize = 1024;

auto trim(std::string_view s) -> std::string_view {
  const auto first = s.find_first_not_of(" \t");
  if (first == std::string_view::npos) return {};
  return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

// A coding with its q: "gzip;q=0.5" → ("gzip", 0.5); no q is 1.
auto coding(std::string_view item) -> std::pair<std::string, double> {
  const auto semicolon = item.find(';');
  auto q = 1.0;
  if (semicolon != std::string_view::npos) {
    auto param = trim(item.substr(semicolon + 1));
    if (param.starts_with("q=") || param.starts_with("Q=")) {
      param.remove_prefix(2);
      const auto [ptr, ec] =
          std::from_chars(param.data(), param.data() + param.size(), q);
      if (ec != std::errc{}) q = 0;  // unreadable: not taken
    }
  }
  std::string name{trim(item.substr(0, semicolon))};
  for (auto& c : name) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return {std::move(name), q};
}

}  // namespace

auto name(Encoding e) -> std::string_view {
  return e == Encoding::kZstd ? "zstd" : "gzip";
}

auto chooseEncoding(std::string_view acceptEncoding)
    -> std::optional<Encoding> {
  std::optional<double> zstd, gzip, any;
  for (const auto part : std::views::split(acceptEncoding, ',')) {
    const auto [name, q] = coding(std::string_view{part});
    if (name == "zstd") zstd = q;
    if (name == "gzip") gzip = q;
    if (name == "*") any = q;
  }
  if (zstd.value_or(0) > 0) return Encoding::kZstd;
  if (gzip.value_or(any.value_or(0)) > 0) return Encoding::kGzip;
  return std::nullopt;
}

auto compressible(std::string_view contentType, std::size_t size) -> bool {
  if (size < kMinSize) return false;
  const auto type = trim(contentType.substr(0, contentType.find(';')));
  return type.starts_with("text/") || type == "application/json" ||
         type == "application/javascript" || type == "image/svg+xml";
}

auto compress(Encoding e, std::string_view data) -> std::string {
  std::string out;
  if (e == Encoding::kZstd) {
    out.resize(ZSTD_compressBound(data.size()));
    const auto n = ZSTD_compress(out.data(), out.size(), data.data(),
                                 data.size(), /*level*/ 3);
    if (ZSTD_isError(n)) {
      throw std::runtime_error(std::string{"zstd: "} + ZSTD_getErrorName(n));
    }
    out.resize(n);
    return out;
  }
  z_stream z{};
  // 15 + 16: a gzip header and trailer around deflate, as browsers expect.
  if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    throw std::runtime_error("gzip: deflateInit2 failed");
  }
  out.resize(deflateBound(&z, static_cast<uLong>(data.size())));
  z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  z.avail_in = static_cast<uInt>(data.size());
  z.next_out = reinterpret_cast<Bytef*>(out.data());
  z.avail_out = static_cast<uInt>(out.size());
  const auto status = deflate(&z, Z_FINISH);
  out.resize(z.total_out);
  deflateEnd(&z);
  if (status != Z_STREAM_END) throw std::runtime_error("gzip: deflate failed");
  return out;
}

void CompressionMiddleware::HandleRequest(
    userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context) const {
  Next(request, context);
  auto& response = request.GetHttpResponse();
  if (response.HasHeader(headers::kContentEncoding)) return;
  if (!compressible(response.GetHeader(headers::kContentType),
                    response.GetData().size())) {
    return;
  }
  // Whether it is compressed depends on the request: caches must know.
  response.SetHeader(headers::kVary, std::string{"Accept-Encoding"});
  const auto e = chooseEncoding(request.GetHeader(headers::kAcceptEncoding));
  if (!e) return;
  response.SetData(compress(*e, response.GetData()));
  response.SetContentEncoding(std::string{name(*e)});
}

}  // namespace factoryctl::http
