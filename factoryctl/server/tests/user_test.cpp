#include "api/user.hpp"

#include <userver/utest/utest.hpp>

namespace {

using factoryctl::api::cleanUser;

TEST(User, TakesTheNameAsItIs) { EXPECT_EQ(cleanUser("pavel"), "pavel"); }

TEST(User, NoHeaderIsNoOne) {
  EXPECT_EQ(cleanUser(""), "");
  EXPECT_EQ(cleanUser("   "), "");
}

TEST(User, TrimsSpaces) { EXPECT_EQ(cleanUser("  pavel \t"), "pavel"); }

TEST(User, DropsControlCharacters) {
  EXPECT_EQ(cleanUser("pa\nvel\x01"), "pavel");
}

TEST(User, CutsLongNamesOnACharacter) {
  std::string name(99, 'a');
  name += "\xd0\xb6";  // 2 bytes: the cut at 100 would split it
  EXPECT_EQ(cleanUser(name), std::string(99, 'a'));
  EXPECT_EQ(cleanUser(std::string(150, 'b')), std::string(100, 'b'));
}

}  // namespace
