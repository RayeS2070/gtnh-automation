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

  auto labels(const StockQuery& q) {
    std::vector<std::string> out;
    for (const auto& r : findStock(db(), q)) out.push_back(r.label);
    return out;
  }
  auto labels(std::string text) { return labels({.text = std::move(text)}); }
};

const ScannedGoods kStick{{"minecraft:stick", 0, ""}, "Stick", 5};
const ScannedGoods kCasing{{"gregtech:gt.blockcasings10", 11, ""},
                           "Extreme Density Space-Bending Casing",
                           1571};
const ScannedGoods kPersulfate{{"ae2fc:fluid_drop", 0, "sodiumpersulfate"},
                               "drop of Sodium Persulfate",
                               1'366'000};

UTEST_F(StockTest, NothingBeforeAScan) { EXPECT_TRUE(labels("").empty()); }

UTEST_F(StockTest, AllByLabelWithoutTheDropPrefix) {
  scan({kStick, kCasing, kPersulfate}, t0);
  EXPECT_EQ(labels(""), (std::vector<std::string>{
                            "Extreme Density Space-Bending Casing",
                            "drop of Sodium Persulfate", "Stick"}));
}

UTEST_F(StockTest, ByLabelDescending) {
  scan({kStick, kCasing, kPersulfate}, t0);
  EXPECT_EQ(labels({.descending = true}),
            (std::vector<std::string>{"Stick", "drop of Sodium Persulfate",
                                      "Extreme Density Space-Bending Casing"}));
}

UTEST_F(StockTest, ByAmount) {
  scan({kStick, kCasing, kPersulfate}, t0);
  EXPECT_EQ(labels({.order = StockOrder::kAmount}),
            (std::vector<std::string>{"Stick",
                                      "Extreme Density Space-Bending Casing",
                                      "drop of Sodium Persulfate"}));
  EXPECT_EQ(labels({.order = StockOrder::kAmount, .descending = true}),
            (std::vector<std::string>{"drop of Sodium Persulfate",
                                      "Extreme Density Space-Bending Casing",
                                      "Stick"}));
}

UTEST_F(StockTest, LimitAfterSorting) {
  scan({kStick, kCasing, kPersulfate}, t0);
  EXPECT_EQ(
      labels({.order = StockOrder::kAmount, .descending = true, .limit = 1}),
      (std::vector<std::string>{"drop of Sodium Persulfate"}));
}

UTEST_F(StockTest, ItemsOrFluids) {
  scan({kStick, kCasing, kPersulfate}, t0);
  EXPECT_EQ(labels({.kind = GoodsKind::kItems}),
            (std::vector<std::string>{"Extreme Density Space-Bending Casing",
                                      "Stick"}));
  EXPECT_EQ(labels({.kind = GoodsKind::kFluids}),
            (std::vector<std::string>{"drop of Sodium Persulfate"}));
}

UTEST_F(StockTest, EveryWordInAnyOrder) {
  scan({kStick,
        {{"gregtech:gt.blockmachines", 1, ""}, "16x Aluminium Cable", 1},
        {{"gregtech:gt.metaitem.01", 2, ""}, "Optical Fiber Cable", 1},
        {{"gregtech:gt.metaitem.01", 3, ""}, "Aluminium Plate", 1}},
       t0);
  const std::vector<std::string> cables{"16x Aluminium Cable",
                                        "Optical Fiber Cable"};
  EXPECT_EQ(labels("al cab"), cables);
  EXPECT_EQ(labels("CAB al"), cables);
  EXPECT_EQ(labels("  al \t cab  "), cables) << "any spaces between words";
}

UTEST_F(StockTest, DropPrefixIsNotSearched) {
  scan({kStick, kPersulfate}, t0);
  EXPECT_TRUE(labels("drop").empty());
  EXPECT_EQ(labels("sodium"),
            (std::vector<std::string>{"drop of Sodium Persulfate"}));
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
  EXPECT_EQ(labels({.limit = 2}).size(), 2);
}

UTEST_F(StockTest, RowCarriesKeyAndAmount) {
  scan({kStick, kCasing, kPersulfate}, t0);
  const auto rows = findStock(db(), {.text = "persulfate", .limit = 10});
  ASSERT_EQ(rows.size(), 1);
  EXPECT_GT(rows[0].goodsId, 0);
  EXPECT_EQ(rows[0].key, kPersulfate.key);
  EXPECT_EQ(rows[0].amount, 1'366'000);
}

UTEST_F(StockTest, GoneFromTheLastScanIsZero) {
  scan({kStick, kCasing}, t0);
  scan({kCasing}, t0 + 5min);
  const auto rows = findStock(db(), {.text = "stick", .limit = 10});
  ASSERT_EQ(rows.size(), 1) << "still listed: it may be ordered";
  EXPECT_EQ(rows[0].amount, 0);
}

}  // namespace
