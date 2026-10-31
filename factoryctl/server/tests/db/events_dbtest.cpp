#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "db/events.hpp"
#include "db/manual.hpp"
#include "db/scan.hpp"
#include "db/schema.hpp"
#include "db/stock.hpp"
#include "db/targets.hpp"
#include "fixture.hpp"

namespace {

using namespace factoryctl::domain;
using namespace factoryctl::db;
using namespace std::chrono_literals;
using factoryctl::test::DbTest;

const TimePoint t0{std::chrono::sys_days{std::chrono::October / 31 / 2026}};
const ItemKey kCasing{"gregtech:gt.blockcasings10", 11, ""};

class EventsTest : public DbTest {
 protected:
  EventsTest() {
    (void)ensureSchema(db());
    const auto id = startScan(db(), t0);
    const std::vector<ScannedGoods> goods{
        {kCasing, "Extreme Density Space-Bending Casing", 1571}};
    (void)storeBatch(db(), id, goods);
    (void)finishScan(db(), id, t0);
    casing = findStock(db(), {.text = "casing", .limit = 1}).at(0).goodsId;
    // The scan above told its finish: each test starts with no events.
    db().Execute(pg::ClusterHostType::kMaster, "DELETE FROM factoryctl.events");
  }

  auto add(std::vector<Event> events) -> void {
    auto tx = db().Begin(pg::ClusterHostType::kMaster, {});
    addEvents(tx, events);
    tx.Commit();
  }

  auto texts(const EventQuery& q) {
    std::vector<std::string> out;
    for (const auto& e : listEvents(db(), q)) out.push_back(e.text);
    return out;
  }

  std::int64_t casing = 0;
};

UTEST_F(EventsTest, NewestFirst) {
  add({{.at = t0, .source = "scan", .text = "scan 1 started"},
       {.at = t0 + 1s, .source = "scan", .text = "scan 1 finished"}});
  const auto rows = listEvents(db(), {});
  ASSERT_EQ(rows.size(), 2);
  EXPECT_EQ(rows[0].text, "scan 1 finished");
  EXPECT_EQ(rows[0].at, t0 + 1s);
  EXPECT_EQ(rows[0].level, "info");
  EXPECT_EQ(rows[0].source, "scan");
  EXPECT_GT(rows[0].id, rows[1].id);
}

UTEST_F(EventsTest, Filters) {
  add({{.at = t0, .level = "debug", .source = "agent", .text = "poll",
        .agentUptime = 12.5},
       {.at = t0, .level = "warning", .source = "agent",
        .text = "probe x: boom"},
       {.at = t0, .level = "error", .source = "server", .text = "Boom"},
       {.at = t0, .source = "scan", .text = "scan 1 started"}});
  EXPECT_EQ(texts({.minLevel = "warning"}),
            (std::vector<std::string>{"Boom", "probe x: boom"}));
  EXPECT_EQ(texts({.source = "agent"}),
            (std::vector<std::string>{"probe x: boom", "poll"}));
  EXPECT_EQ(texts({.text = "BOOM"}),
            (std::vector<std::string>{"Boom", "probe x: boom"}))
      << "any case";
  EXPECT_EQ(listEvents(db(), {.source = "agent"}).back().agentUptime, 12.5);
}

UTEST_F(EventsTest, PagesByIdAndLimit) {
  add({{.at = t0, .source = "scan", .text = "1"},
       {.at = t0, .source = "scan", .text = "2"},
       {.at = t0, .source = "scan", .text = "3"}});
  const auto first = listEvents(db(), {.limit = 2});
  EXPECT_EQ(first.size(), 2);
  EXPECT_EQ(texts({.before = first.back().id, .limit = 2}),
            (std::vector<std::string>{"1"}));
}

UTEST_F(EventsTest, GoodsByKeyOrFromTheOrder) {
  const auto id = orderOnce(db(), casing, 128, "Pavel", t0);
  ASSERT_TRUE(id.has_value()) << id.error();
  add({{.at = t0, .source = "order", .text = "sent", .order = *id},
       {.at = t0, .level = "warning", .source = "server",
        .text = "probe ignored", .goods = kCasing},
       {.at = t0, .source = "server", .text = "unknown goods",
        .goods = ItemKey{"a:none", 0, ""}}});
  const auto rows = listEvents(db(), {.source = "server"});
  ASSERT_EQ(rows.size(), 2);
  EXPECT_FALSE(rows[0].goods.has_value()) << "unknown goods: none";
  ASSERT_TRUE(rows[1].goods.has_value());
  EXPECT_EQ(rows[1].goods->id, casing);
  EXPECT_EQ(rows[1].goods->label, "Extreme Density Space-Bending Casing");

  const auto history = listEvents(db(), {.order = *id});
  ASSERT_FALSE(history.empty());
  EXPECT_EQ(history[0].text, "sent");
  ASSERT_TRUE(history[0].goods.has_value()) << "the order's goods";
  EXPECT_EQ(history[0].goods->key, kCasing);
}

UTEST_F(EventsTest, OldOnesGo) {
  add({{.at = t0 - 31 * 24h, .source = "scan", .text = "old"},
       {.at = t0, .source = "scan", .text = "new"}});
  EXPECT_EQ(deleteEventsBefore(db(), t0 - 30 * 24h), 1);
  EXPECT_EQ(texts({}), (std::vector<std::string>{"new"}));
}

// --- who tells what ------------------------------------------------------------

UTEST_F(EventsTest, OneOffOrderAndCancelAreTold) {
  const auto id = orderOnce(db(), casing, 128, "Pavel", t0);
  ASSERT_TRUE(id.has_value()) << id.error();
  ASSERT_TRUE(cancelOrder(db(), *id, t0 + 1s));
  EXPECT_EQ(texts({.order = *id}),
            (std::vector<std::string>{"canceled before it reached the agent",
                                      "queued 128 by Pavel"}));
  EXPECT_EQ(listEvents(db(), {.order = *id}).at(0).goods->id, casing);
}

UTEST_F(EventsTest, RefusedOrderIsNotTold) {
  ASSERT_TRUE(orderOnce(db(), casing, 1, "", t0));
  ASSERT_FALSE(orderOnce(db(), casing, 1, "", t0)) << "already on order";
  EXPECT_EQ(texts({.source = "order"}),
            (std::vector<std::string>{"queued 1"}));
}

UTEST_F(EventsTest, TargetChangesAreTold) {
  ASSERT_TRUE(setTarget(db(), casing,
                        {.threshold = 100'000,
                         .batch = 5000,
                         .priority = Priority::kLow,
                         .fill = true},
                        t0));
  ASSERT_TRUE(setTarget(db(), casing,
                        {.threshold = 0, .batch = 64, .enabled = false}, t0));
  ASSERT_TRUE(deleteTarget(db(), casing, t0));
  EXPECT_EQ(texts({.source = "target"}),
            (std::vector<std::string>{
                "target removed",
                "target: order 64 whenever nothing is on order, paused",
                "target: keep 100000, order 5000, low priority, fill"}));
  EXPECT_EQ(listEvents(db(), {.source = "target"}).at(0).goods->id, casing);
}

UTEST_F(EventsTest, FinishedScanIsTold) {
  const auto id = startScan(db(), t0 + 5min);
  const std::vector<ScannedGoods> goods{
      {kCasing, "Extreme Density Space-Bending Casing", 1571}};
  (void)storeBatch(db(), id, goods);
  ASSERT_TRUE(finishScan(db(), id, t0 + 5min + 9s));
  EXPECT_EQ(texts({.source = "scan"}),
            (std::vector<std::string>{
                std::format("scan {} finished: 1 goods seen", id)}));
}

}  // namespace
