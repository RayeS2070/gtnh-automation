#include <fstream>
#include <map>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>

#include "db/scan.hpp"
#include "db/schema.hpp"
#include "fixture.hpp"

namespace {

using namespace factoryctl::domain;
using namespace factoryctl::db;
using namespace std::chrono_literals;
using factoryctl::test::DbTest;

const ItemKey kStick{"minecraft:stick", 0, ""};
const ItemKey kHydrogen{"ae2fc:fluid_drop", 0, "hydrogen"};
const TimePoint t0{std::chrono::sys_days{std::chrono::October / 30 / 2026}};

auto goods(const ItemKey& key, std::int64_t size) -> ScannedGoods {
  return {key, key.variant.empty() ? "Stick" : "drop of Hydrogen", size};
}

class ScanTest : public DbTest {
 protected:
  ScanTest() { (void)ensureSchema(db()); }

  // A whole scan in one batch.
  auto scan(std::initializer_list<ScannedGoods> batch, TimePoint at = t0)
      -> std::int64_t {
    const auto id = startScan(db(), at);
    const std::vector<ScannedGoods> v(batch);
    EXPECT_TRUE(storeBatch(db(), id, v).has_value());
    EXPECT_TRUE(finishScan(db(), id, at + 2s).has_value());
    return id;
  }

  auto count(const char* sql) -> std::int64_t {
    return db()
        .Execute(pg::ClusterHostType::kMaster, sql)
        .AsSingleRow<std::int64_t>();
  }
};

UTEST_F(ScanTest, NoStockBeforeAFinishedScan) {
  EXPECT_EQ(currentStock(db()), std::nullopt);
  const auto id = startScan(db(), t0);
  const std::vector batch{goods(kStick, 5)};
  ASSERT_TRUE(storeBatch(db(), id, batch).has_value());
  EXPECT_EQ(currentStock(db()), std::nullopt) << "not finished yet";
}

UTEST_F(ScanTest, FinishedScanGivesTheStock) {
  scan({goods(kStick, 5), goods(kHydrogen, 1000)});
  EXPECT_EQ(currentStock(db()), (Stock{{kStick, 5}, {kHydrogen, 1000}}));
}

UTEST_F(ScanTest, ZeroIsStockToo) {
  scan({goods(kStick, 0)});
  EXPECT_EQ(currentStock(db()), (Stock{{kStick, 0}}));
}

UTEST_F(ScanTest, AbsentFromTheNextScanIsGone) {
  scan({goods(kStick, 5), goods(kHydrogen, 1000)});
  scan({goods(kHydrogen, 900)}, t0 + 1min);
  EXPECT_EQ(currentStock(db()), (Stock{{kHydrogen, 900}}));
}

UTEST_F(ScanTest, RunningScanDoesNotZeroWhatItHasNotReached) {
  scan({goods(kStick, 5), goods(kHydrogen, 1000)});
  const auto id = startScan(db(), t0 + 1min);
  const std::vector batch{goods(kHydrogen, 7)};
  ASSERT_TRUE(storeBatch(db(), id, batch).has_value());
  EXPECT_EQ(currentStock(db()), (Stock{{kStick, 5}, {kHydrogen, 7}}));
}

UTEST_F(ScanTest, LatestLabelIsKeptUnderTheSameKey) {
  scan({{kStick, "Stick", 5}});
  scan({{kStick, "Wooden Stick", 6}}, t0 + 1min);
  EXPECT_EQ(count("SELECT count(*) FROM factoryctl.goods"), 1);
  EXPECT_EQ(db().Execute(pg::ClusterHostType::kMaster,
                         "SELECT label FROM factoryctl.goods")
                .AsSingleRow<std::string>(),
            "Wooden Stick");
}

UTEST_F(ScanTest, BatchesAddUp) {
  const auto id = startScan(db(), t0);
  const std::vector first{goods(kStick, 5)};
  const std::vector second{goods(kHydrogen, 1000)};
  ASSERT_TRUE(storeBatch(db(), id, first).has_value());
  ASSERT_TRUE(storeBatch(db(), id, second).has_value());
  ASSERT_TRUE(finishScan(db(), id, t0 + 2s).has_value());
  EXPECT_EQ(currentStock(db()), (Stock{{kStick, 5}, {kHydrogen, 1000}}));
}

UTEST_F(ScanTest, FinishedOrUnknownScanRefusesBatches) {
  const auto id = scan({goods(kStick, 5)});
  const std::vector batch{goods(kStick, 1)};
  EXPECT_FALSE(storeBatch(db(), id, batch).has_value());
  EXPECT_FALSE(storeBatch(db(), id + 100, batch).has_value());
  EXPECT_FALSE(finishScan(db(), id, t0 + 1min).has_value());
  EXPECT_FALSE(finishScan(db(), id + 100, t0 + 1min).has_value());
  EXPECT_EQ(currentStock(db()), (Stock{{kStick, 5}}));
}

UTEST_F(ScanTest, FinishWritesHistoryOfTargets) {
  scan({goods(kStick, 5), goods(kHydrogen, 1000)});
  db().Execute(pg::ClusterHostType::kMaster,
               "INSERT INTO factoryctl.targets (goods_id, threshold, batch) "
               "SELECT id, 10, 100 FROM factoryctl.goods");
  scan({goods(kHydrogen, 900)}, t0 + 1min);
  EXPECT_EQ(count("SELECT count(*) FROM factoryctl.stock_history"), 2);
  EXPECT_EQ(count("SELECT amount FROM factoryctl.stock_history h "
                  "JOIN factoryctl.goods g ON g.id = h.goods_id "
                  "WHERE g.name = 'minecraft:stick'"),
            0)
      << "a target absent from the scan has 0";
  EXPECT_EQ(count("SELECT amount FROM factoryctl.stock_history h "
                  "JOIN factoryctl.goods g ON g.id = h.goods_id "
                  "WHERE g.variant = 'hydrogen'"),
            900);
}

// A sample of a real network (tests/data/me-sample.jsonl, from
// tools/me-dump.lua): fluid drops, keys repeated by NBT or by a stored stack
// next to its craftable entry, colour codes in labels, sizes up to 2^52.
UTEST_F(ScanTest, ReplaysARealDump) {
  namespace json = userver::formats::json;
  std::map<std::int64_t, std::vector<ScannedStack>> batches;  // by chunk
  std::ifstream in(FACTORYCTL_TEST_DATA "/me-sample.jsonl");
  ASSERT_TRUE(in) << "no test data";
  for (std::string line; std::getline(in, line);) {
    const auto v = json::FromString(line);
    batches[v["_chunk"].As<std::int64_t>()].push_back(
        {v["name"].As<std::string>(), v["damage"].As<std::int32_t>(),
         v["label"].As<std::string>(), v["size"].As<std::int64_t>(),
         v["fluidDrop"]["name"].As<std::string>("")});
  }
  ASSERT_EQ(batches.size(), 20);

  const auto id = startScan(db(), t0);
  std::size_t keys = 0;
  for (const auto& [chunk, stacks] : batches) {
    const auto merged = mergeBatch(stacks);
    keys += merged.size();
    ASSERT_TRUE(storeBatch(db(), id, merged).has_value()) << chunk;
  }
  ASSERT_TRUE(finishScan(db(), id, t0 + 2s).has_value());

  const auto stock = currentStock(db());
  ASSERT_TRUE(stock.has_value());
  EXPECT_EQ(stock->size(), keys);
  // A stored MFE (with NBT) and its craftable entry: one key, 1 + 0.
  EXPECT_EQ(stock->at({"IC2:blockElectric", 1, ""}), 1);
  // The largest number OC hands out: 2^52 - 1.
  EXPECT_EQ(stock->at({"ae2fc:fluid_drop", 0, "water"}), 4'503'599'627'370'495);
}

}  // namespace
