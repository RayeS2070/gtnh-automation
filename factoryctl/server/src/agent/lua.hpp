#pragma once

#include <expected>
#include <string>
#include <userver/formats/json/value.hpp>

namespace factoryctl::agent {

// Responses to the agent are Lua table literals: OpenOS reads them with
// serialization.unserialize, so the agent needs no JSON parser.
//
// Objects become {key=value} (or ["key"]=value when the key is not a Lua
// name), arrays {v1, v2}. Fails on what Lua cannot hold: null inside an array
// (a hole that breaks #t and ipairs) and integers beyond int64. A null field
// of an object is left out, which in Lua is the same as nil.
auto toLua(const userver::formats::json::Value& value)
    -> std::expected<std::string, std::string>;

}  // namespace factoryctl::agent
