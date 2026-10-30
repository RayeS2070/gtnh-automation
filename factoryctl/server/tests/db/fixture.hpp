#pragma once

// Database tests run against a real PostgreSQL: POSTGRES_TEST_DSN, set by
// CMake to the factoryctl_test database of compose.yaml.

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/utest/postgres_fixture.hpp>

namespace factoryctl::test {

namespace pg = userver::storages::postgres;

// Every test starts without the schema.
class DbTest : public pg::utest::PostgresTest {
 protected:
  DbTest() { dropSchema(); }

  auto db() -> pg::Cluster& { return *GetCluster(); }

  auto dropSchema() -> void {
    db().Execute(pg::ClusterHostType::kMaster,
                 "DROP SCHEMA IF EXISTS factoryctl CASCADE");
  }
};

}  // namespace factoryctl::test
