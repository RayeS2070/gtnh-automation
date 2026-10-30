#include "db/sql.hpp"

#include <userver/utest/utest.hpp>

namespace {

using factoryctl::db::schemaVersion;
using factoryctl::db::splitStatements;
using Statements = std::vector<std::string>;

TEST(Sql, SplitsOnSemicolons) {
  EXPECT_EQ(splitStatements("CREATE SCHEMA a; CREATE TABLE a.t (x int);"),
            (Statements{"CREATE SCHEMA a", "CREATE TABLE a.t (x int)"}));
}

TEST(Sql, LastStatementWithoutSemicolon) {
  EXPECT_EQ(splitStatements("SELECT 1;\nSELECT 2"),
            (Statements{"SELECT 1", "SELECT 2"}));
}

TEST(Sql, TrimsAndDropsEmpty) {
  EXPECT_EQ(splitStatements("\n  SELECT 1 ;\n\n;  ;\n"),
            (Statements{"SELECT 1"}));
  EXPECT_TRUE(splitStatements("").empty());
}

TEST(Sql, KeepsLinesOfAStatement) {
  EXPECT_EQ(splitStatements("CREATE TABLE t (\n  x int\n);"),
            (Statements{"CREATE TABLE t (\n  x int\n)"}));
}

TEST(Sql, DropsComments) {
  EXPECT_EQ(splitStatements("-- head; with a semicolon\n"
                            "SELECT 1; -- tail;\n"
                            "SELECT 2 -- inside\n  + 3;"),
            (Statements{"SELECT 1", "SELECT 2 \n  + 3"}));
}

TEST(Sql, SemicolonInStringDoesNotSplit) {
  EXPECT_EQ(splitStatements("INSERT INTO t VALUES ('a;b');"),
            (Statements{"INSERT INTO t VALUES ('a;b')"}));
}

TEST(Sql, DashesInStringAreNotAComment) {
  EXPECT_EQ(splitStatements("SELECT '--x'; SELECT 2;"),
            (Statements{"SELECT '--x'", "SELECT 2"}));
}

TEST(Sql, DoubledQuoteStaysInString) {
  EXPECT_EQ(splitStatements("SELECT 'it''s; fine'; SELECT 2;"),
            (Statements{"SELECT 'it''s; fine'", "SELECT 2"}));
}

TEST(Sql, ApostropheInCommentIsIgnored) {
  EXPECT_EQ(splitStatements("-- don't\nSELECT 1;"), (Statements{"SELECT 1"}));
}

TEST(Sql, SchemaVersion) {
  EXPECT_EQ(schemaVersion("CREATE SCHEMA factoryctl;\n"
                          "INSERT INTO factoryctl.schema_version VALUES (3);"),
            3);
  EXPECT_EQ(schemaVersion("INSERT INTO factoryctl.schema_version VALUES (12)"),
            12);
}

TEST(Sql, NoSchemaVersion) {
  EXPECT_EQ(schemaVersion("CREATE SCHEMA factoryctl;"), std::nullopt);
  EXPECT_EQ(schemaVersion("INSERT INTO factoryctl.schema_version VALUES (x)"),
            std::nullopt);
}

// The real schema file, as embedded into the server.
TEST(Sql, SchemaFileIsReadable) {
  const auto sql = factoryctl::db::schemaSql();
  EXPECT_EQ(schemaVersion(sql), 3);
  const auto statements = splitStatements(sql);
  ASSERT_FALSE(statements.empty());
  EXPECT_EQ(statements.front(), "CREATE SCHEMA factoryctl");
  for (const auto& s : statements) {
    EXPECT_EQ(s.find("--"), std::string::npos) << s;
  }
}

}  // namespace
