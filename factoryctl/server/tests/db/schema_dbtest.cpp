#include <userver/storages/postgres/exceptions.hpp>

#include "db/schema.hpp"
#include "fixture.hpp"

namespace {

using factoryctl::db::ensureSchema;
using factoryctl::db::SchemaOutcome;
using factoryctl::test::DbTest;
namespace pg = userver::storages::postgres;

UTEST_F(DbTest, EmptyDatabaseGetsTheSchema) {
  EXPECT_EQ(ensureSchema(db()), SchemaOutcome::kCreated);
  const auto tables =
      db().Execute(pg::ClusterHostType::kMaster,
                   "SELECT count(*) FROM information_schema.tables "
                   "WHERE table_schema = 'factoryctl'")
          .AsSingleRow<std::int64_t>();
  EXPECT_EQ(tables, 8);
}

UTEST_F(DbTest, SameVersionIsKept) {
  ASSERT_EQ(ensureSchema(db()), SchemaOutcome::kCreated);
  EXPECT_EQ(ensureSchema(db()), SchemaOutcome::kReady);
}

UTEST_F(DbTest, OtherVersionIsRefused) {
  ASSERT_EQ(ensureSchema(db()), SchemaOutcome::kCreated);
  db().Execute(pg::ClusterHostType::kMaster,
               "UPDATE factoryctl.schema_version SET version = 99");
  const auto outcome = ensureSchema(db());
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().find("DROP SCHEMA factoryctl CASCADE"),
            std::string::npos)
      << outcome.error();
}

// The rule that ties an order to its CPU by item lives in the database too.
UTEST_F(DbTest, OneOpenOrderPerGoods) {
  ASSERT_EQ(ensureSchema(db()), SchemaOutcome::kCreated);
  const auto exec = [&](const char* sql) {
    db().Execute(pg::ClusterHostType::kMaster, sql);
  };
  exec(
      "INSERT INTO factoryctl.goods (kind, name, label) "
      "VALUES ('item', 'minecraft:stick', 'Stick')");
  exec(
      "INSERT INTO factoryctl.orders (goods_id, amount, source, status) "
      "VALUES (1, 5, 'auto', 'done')");
  exec(
      "INSERT INTO factoryctl.orders (goods_id, amount, source) "
      "VALUES (1, 1000, 'auto')");
  EXPECT_THROW(exec("INSERT INTO factoryctl.orders (goods_id, amount, source) "
                    "VALUES (1, 7, 'manual')"),
               pg::UniqueViolation);
}

}  // namespace
