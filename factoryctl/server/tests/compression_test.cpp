#include "http/compression.hpp"

#include <zlib.h>

#include <userver/compression/zstd.hpp>
#include <userver/utest/utest.hpp>

namespace factoryctl::http {
namespace {

auto gunzip(std::string_view data, std::size_t size) -> std::string {
  z_stream z{};
  EXPECT_EQ(inflateInit2(&z, 15 + 32), Z_OK);
  std::string out(size + 64, '\0');
  z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  z.avail_in = static_cast<uInt>(data.size());
  z.next_out = reinterpret_cast<Bytef*>(out.data());
  z.avail_out = static_cast<uInt>(out.size());
  EXPECT_EQ(inflate(&z, Z_FINISH), Z_STREAM_END);
  out.resize(z.total_out);
  inflateEnd(&z);
  return out;
}

auto sample() -> std::string {
  std::string s;
  for (int i = 0; i < 2000; ++i) {
    s += R"({"goodsId":)" + std::to_string(i) +
         R"(,"label":"Long Lanthanum Hexaboride Rod","amount":60659},)";
  }
  return s;
}

TEST(Compression, ChoosesFromAcceptEncoding) {
  EXPECT_EQ(chooseEncoding(""), std::nullopt);
  EXPECT_EQ(chooseEncoding("identity"), std::nullopt);
  EXPECT_EQ(chooseEncoding("gzip, deflate, br, zstd"), Encoding::kZstd);
  EXPECT_EQ(chooseEncoding("gzip"), Encoding::kGzip);
  EXPECT_EQ(chooseEncoding("deflate, GZIP"), Encoding::kGzip);
  EXPECT_EQ(chooseEncoding("gzip;q=0.5, zstd;q=0"), Encoding::kGzip);
  EXPECT_EQ(chooseEncoding("zstd; q=0.0, gzip ;q=0"), std::nullopt);
  EXPECT_EQ(chooseEncoding("gzip;q=1.0"), Encoding::kGzip);
  EXPECT_EQ(chooseEncoding("*"), Encoding::kGzip);
  EXPECT_EQ(chooseEncoding("br"), std::nullopt);
}

TEST(Compression, OnlyTextOfSomeSize) {
  EXPECT_TRUE(compressible("application/json", 2000));
  EXPECT_TRUE(compressible("text/html; charset=utf-8", 2000));
  EXPECT_TRUE(compressible("text/javascript", 2000));
  EXPECT_TRUE(compressible("text/css", 2000));
  EXPECT_TRUE(compressible("image/svg+xml", 2000));
  EXPECT_TRUE(compressible("text/plain; charset=utf-8", 2000));
  EXPECT_FALSE(compressible("application/json", 100)) << "too small to pay";
  EXPECT_FALSE(compressible("image/webp", 2000));
  EXPECT_FALSE(compressible("application/octet-stream", 2000)) << "index.bin";
  EXPECT_FALSE(compressible("font/woff2", 2000));
  EXPECT_FALSE(compressible("", 2000));
}

TEST(Compression, GzipRoundTrip) {
  const auto data = sample();
  const auto packed = compress(Encoding::kGzip, data);
  EXPECT_LT(packed.size(), data.size() / 5);
  EXPECT_EQ(gunzip(packed, data.size()), data);
}

TEST(Compression, ZstdRoundTrip) {
  const auto data = sample();
  const auto packed = compress(Encoding::kZstd, data);
  EXPECT_LT(packed.size(), data.size() / 5);
  EXPECT_EQ(userver::compression::zstd::Decompress(packed, data.size()), data);
}

TEST(Compression, Names) {
  EXPECT_EQ(name(Encoding::kGzip), "gzip");
  EXPECT_EQ(name(Encoding::kZstd), "zstd");
}

}  // namespace
}  // namespace factoryctl::http
