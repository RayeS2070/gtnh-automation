#include <spdlog/spdlog.h>

#include <boost/program_options.hpp>
#include <print>

auto main() -> int {
  spdlog::set_level(spdlog::level::debug);

  spdlog::debug("Hello, world!");
  std::print("Hello, world!");
  return 0;
}
