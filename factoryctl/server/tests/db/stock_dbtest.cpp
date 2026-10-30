#include "db/scan.hpp"
#include "db/schema.hpp"
#include "db/stock.hpp"
#include "fixture.hpp"

namespace {

using namespace factoryctl::domain;
using namespace factoryctl::db;
using namespace std::chrono_literals;
using factoryctl::test::DbTest;

const TimePoint t0{std::chrono::sys_days{std::chrono::October / 30 / 2026}};

class StockTest : public DbTest {
 protected:
  StockTest() { (void)ensureSchema(db()); }

  auto scan(std::vector<ScannedGoods> goods, TimePoint at) {
    const auto id = startScan(db(), at);
    (void)storeBatch(db(), id, goods);
    (void)finishScan(db(), id, at);
  }

  auto labels(std::string_view q, std::int64_t limit = 100) {
    std::vector<std::string> out;
    for (const auto& r : findStock(db(), q, limit)) out.push_back(r.label);
    return out;
  }
};

const ScannedGoods kStick{{"minecraft:stick", 0, ""}, "Stick", 5};
const ScannedGoods kCasing{{"gregtech:gt.blockcasings10", 11, ""},
                           "Extreme Density Space-Bending Casing",
                           1571};
const ScannedGoods kPersulfate{{"ae2fc:fluid_drop", 0, "sodiumpersulfate"},
                               "drop of Sodium Persulfate",
                               1'366'000};

UTEST_F(StockTest, NothingBeforeAScan) { EXPECT_TRUE(labels("").empty()); }

UTEST_F(StockTest, AllByLabel) {
  scan({kStick, kCasing, kPersulfate}, t0);
  EXPECT_EQ(labels(""), (std::vector<std::string>{
                            "drop of Sodium Persulfate",
                            "Extreme Density Space-Bending Casing", "Stick"}));
}

UTEST_F(StockTest, SearchIgnoresCase) {
  scan({kStick, kCasing, kPersulfate}, t0);
  EXPECT_EQ(labels("SPACE-bend"),
            (std::vector<std::string>{"Extreme Density Space-Bending Casing"}));
  EXPECT_EQ(labels("persulf"),
            (std::vector<std::string>{"drop of Sodium Persulfate"}));
}

UTEST_F(StockTest, PatternCharactersAreLiteral) {
  scan({kStick, {{"a:b", 0, ""}, "100% Pure_Thing", 1}}, t0);
  EXPECT_EQ(labels("100%"), (std::vector<std::string>{"100% Pure_Thing"}));
  EXPECT_TRUE(labels("%").size() == 1) << "% matches itself only";
  EXPECT_TRUE(labels("s_ick").empty()) << "_ is no wildcard";
}

UTEST_F(StockTest, Limit) {
  scan({kStick, kCasing, kPersulfate}, t0);
  EXPECT_EQ(labels("", 2).size(), 2);
}

UTEST_F(StockTest, RowCarriesKeyAndAmount) {
  scan({kStick, kCasing, kPersulfate}, t0);
  const auto rows = findStock(db(), "persulfate", 10);
  ASSERT_EQ(rows.size(), 1);
  EXPECT_GT(rows[0].goodsId, 0);
  EXPECT_EQ(rows[0].key, kPersulfate.key);
  EXPECT_EQ(rows[0].amount, 1'366'000);
}

UTEST_F(StockTest, GoneFromTheLastScanIsZero) {
  scan({kStick, kCasing}, t0);
  scan({kCasing}, t0 + 5min);
  const auto rows = findStock(db(), "stick", 10);
  ASSERT_EQ(rows.size(), 1) << "still listed: it may be ordered";
  EXPECT_EQ(rows[0].amount, 0);
}

}  // namespace
