#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace factoryctl::db {

// schema/schema.sql as embedded into the binary.
auto schemaSql() -> std::string;

// Splits an SQL script into statements: the driver sends one statement per
// call. Knows -- comments and 'strings' (with '' inside); a semicolon in
// either does not split. Dollar quoting ($$) is not supported: the script
// must not use it. Statements come back trimmed, comments and empty ones
// dropped.
auto splitStatements(std::string_view sql) -> std::vector<std::string>;

// The number in `INSERT INTO factoryctl.schema_version VALUES (<n>)`: the
// schema file is the only place that states its version.
auto schemaVersion(std::string_view sql) -> std::optional<int>;

}  // namespace factoryctl::db
