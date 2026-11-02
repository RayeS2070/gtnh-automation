#pragma once

// The mining catalog (data/mining_catalog.json, made by
// factoryctl/mining/tools/mining-catalog.py): smc's asteroids with the item
// ids of their outputs, the drones and their drills, the plasmas.

#include <expected>
#include <string>
#include <userver/formats/json/value.hpp>

#include "domain/mining.hpp"

namespace factoryctl::mining {

auto parseCatalog(const userver::formats::json::Value& json)
    -> std::expected<domain::MiningCatalog, std::string>;

// The one built into the server, read once. A bad file is a bug of the build:
// it throws.
auto catalog() -> const domain::MiningCatalog&;

}  // namespace factoryctl::mining
