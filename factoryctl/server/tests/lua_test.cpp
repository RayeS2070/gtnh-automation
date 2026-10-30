#include "agent/lua.hpp"

#include <userver/formats/json/serialize.hpp>
#include <userver/utest/utest.hpp>

namespace {

using factoryctl::agent::toLua;
namespace json = userver::formats::json;

auto lua(std::string_view text) -> std::string {
  const auto result = toLua(json::FromString(text));
  EXPECT_TRUE(result.has_value()) << text << ": " << result.error_or("");
  return result.value_or("");
}

TEST(Lua, Scalars) {
  EXPECT_EQ(lua("true"), "true");
  EXPECT_EQ(lua("false"), "false");
  EXPECT_EQ(lua("null"), "nil");
  EXPECT_EQ(lua("42"), "42");
  EXPECT_EQ(lua("-7"), "-7");
  EXPECT_EQ(lua("9223372036854775807"), "9223372036854775807");
  EXPECT_EQ(lua("1.5"), "1.5");
}

TEST(Lua, Floats) {
  EXPECT_EQ(lua("0.1"), "0.1");
  EXPECT_EQ(lua("-2.25"), "-2.25");
  EXPECT_EQ(lua("1e300"), "1e+300");
  // userver reports an integral double as an integer (IsInt64), so 1.0 and 1
  // look the same: both become a Lua integer. Amounts are integers anyway.
  EXPECT_EQ(lua("1.0"), "1");
}

TEST(Lua, IntegerBeyondInt64Fails) {
  // Lua 5.3 would read it as a float and lose precision.
  EXPECT_FALSE(toLua(json::FromString("18446744073709551615")).has_value());
}

TEST(Lua, Strings) {
  EXPECT_EQ(lua(R"("plain")"), R"("plain")");
  EXPECT_EQ(lua(R"("say \"hi\"")"), R"("say \"hi\"")");
  EXPECT_EQ(lua(R"("back\\slash")"), R"("back\\slash")");
  EXPECT_EQ(lua(R"("two\nlines")"), R"("two\nlines")");
  EXPECT_EQ(lua(R"("tab\there")"), R"("tab\9here")");
  EXPECT_EQ(lua(R"("nul\u0000x")"), R"("nul\0x")");
  // \ddd takes up to three digits: a digit after the escape must not join it.
  EXPECT_EQ(lua(R"("\u00011")"), R"("\0011")");
  // Bytes above 127 are kept: OC labels may be UTF-8.
  EXPECT_EQ(lua(R"("Тест")"), R"("Тест")");
}

TEST(Lua, Arrays) {
  EXPECT_EQ(lua("[]"), "{}");
  EXPECT_EQ(lua("[1,2,3]"), "{1,2,3}");
  EXPECT_EQ(lua(R"([[1],["a"]])"), R"({{1},{"a"}})");
}

TEST(Lua, NullInArrayFails) {
  EXPECT_FALSE(toLua(json::FromString("[1,null,3]")).has_value());
}

TEST(Lua, Objects) {
  EXPECT_EQ(lua("{}"), "{}");
  EXPECT_EQ(lua(R"({"a":1,"b_2":"x"})"), R"({a=1,b_2="x"})");
  EXPECT_EQ(lua(R"({"order":{"id":5}})"), R"({order={id=5}})");
}

TEST(Lua, KeysThatAreNotNames) {
  EXPECT_EQ(lua(R"({"gregtech:gt.metaitem.01":1})"),
            R"({["gregtech:gt.metaitem.01"]=1})");
  EXPECT_EQ(lua(R"({"2x":1})"), R"({["2x"]=1})");
  EXPECT_EQ(lua(R"({"":1})"), R"({[""]=1})");
  // Reserved words are not names either.
  EXPECT_EQ(lua(R"({"end":1,"nil":2,"goto":3})"),
            R"({["end"]=1,["nil"]=2,["goto"]=3})");
}

TEST(Lua, NullFieldIsLeftOut) {
  EXPECT_EQ(lua(R"({"a":null,"b":1})"), "{b=1}");
}

TEST(Lua, ErrorNamesThePath) {
  const auto result = toLua(json::FromString(R"({"tasks":[1,null]})"));
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("tasks"), std::string::npos) << result.error();
}

}  // namespace
