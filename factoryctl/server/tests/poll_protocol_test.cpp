#include <userver/formats/json/serialize.hpp>
#include <userver/utest/utest.hpp>

#include "agent/protocol.hpp"

namespace {

using namespace factoryctl::agent;
using namespace factoryctl::domain;
using namespace std::chrono_literals;
namespace json = userver::formats::json;

auto poll(std::string_view text) { return parsePoll(json::FromString(text)); }

auto error(std::string_view text) -> std::string {
  const auto r = poll(text);
  EXPECT_FALSE(r.has_value()) << text;
  return r.has_value() ? "" : r.error();
}

// --- the report ------------------------------------------------------------

TEST(PollProtocol, CpusAndResults) {
  // CPU_022 as it came from the game: busy=false, the craft already shown.
  const auto r = poll(R"({"cpus": [
      {"name": "CPU_022", "busy": false,
       "output": {"name": "ae2fc:fluid_drop", "damage": 0,
                  "label": "drop of Xenoxene", "size": 1000000,
                  "fluid": "xenoxene"}},
      {"name": "CPU_031", "busy": true,
       "output": {"name": "dreamcraft:item.TCetiESeaweedExtract", "damage": 0,
                  "label": "TCetiE-Seaweed Extract", "size": 1929}},
      {"name": "CPU_018", "busy": false}],
    "results": [{"order": 17, "status": "accepted"},
                {"order": 18, "status": "failed", "message": "ambiguous"}]})");
  ASSERT_TRUE(r.has_value()) << r.error();
  ASSERT_EQ(r->cpus.size(), 3);
  EXPECT_EQ(r->cpus[0].name, "CPU_022");
  EXPECT_FALSE(r->cpus[0].busy);
  EXPECT_EQ(r->cpus[0].crafting, (ItemKey{"ae2fc:fluid_drop", 0, "xenoxene"}));
  EXPECT_EQ(r->cpus[0].remaining, 1'000'000);
  EXPECT_EQ(r->cpus[1].crafting,
            (ItemKey{"dreamcraft:item.TCetiESeaweedExtract", 0, ""}));
  EXPECT_EQ(r->cpus[2].crafting, std::nullopt);
  ASSERT_EQ(r->results.size(), 2);
  EXPECT_EQ(r->results[0].order, 17);
  EXPECT_EQ(r->results[0].status, AgentStatus::kAccepted);
  EXPECT_EQ(r->results[1].status, AgentStatus::kFailed);
  EXPECT_EQ(r->results[1].message, "ambiguous");
}

TEST(PollProtocol, EveryStatus) {
  const std::pair<const char*, AgentStatus> cases[] = {
      {"computing", AgentStatus::kComputing},
      {"accepted", AgentStatus::kAccepted},
      {"failed", AgentStatus::kFailed},
      {"canceled", AgentStatus::kCanceled},
      {"done", AgentStatus::kDone}};
  for (const auto& [name, status] : cases) {
    const auto r = poll(
        std::string(R"({"cpus": [], "results": [{"order": 1, "status": ")") +
        name + R"("}]})");
    ASSERT_TRUE(r.has_value()) << name << ": " << r.error();
    EXPECT_EQ(r->results[0].status, status) << name;
  }
}

TEST(PollProtocol, NoResults) {
  const auto r = poll(R"({"cpus": []})");
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_TRUE(r->results.empty());
}

TEST(PollProtocol, ErrorsNameTheField) {
  EXPECT_NE(error(R"({})").find("cpus"), std::string::npos);
  EXPECT_NE(error(R"({"cpus": [{"busy": true}]})").find("cpus[0].name"),
            std::string::npos);
  EXPECT_NE(error(R"({"cpus": [{"name": "C"}]})").find("cpus[0].busy"),
            std::string::npos);
  EXPECT_NE(
      error(
          R"({"cpus": [{"name": "C", "busy": true, "output": {"name": "a:b"}}]})")
          .find("cpus[0].output"),
      std::string::npos);
  EXPECT_NE(error(R"({"cpus": [], "results": [{"status": "done"}]})")
                .find("results[0].order"),
            std::string::npos);
  EXPECT_NE(
      error(R"({"cpus": [], "results": [{"order": 1, "status": "lost"}]})")
          .find("results[0].status"),
      std::string::npos);
}

// --- the reply -------------------------------------------------------------

auto sent(std::int64_t id, ItemKey item, std::string label, std::int64_t amount)
    -> OrderState {
  return {.id = id,
          .item = std::move(item),
          .label = std::move(label),
          .amount = amount};
}

TEST(PollProtocol, ReplyNothingToDo) {
  EXPECT_EQ(replyPoll({.poll = 10s}), "{poll=10,orders={},cancel={}}");
}

TEST(PollProtocol, ReplyOrdersWithFilters) {
  const PollReply reply{
      .poll = 10s,
      .send = {sent(18, {"minecraft:stick", 0, ""}, "Stick", 1000),
               sent(19, {"ae2fc:fluid_drop", 0, "xenoxene"}, "drop of Xenoxene",
                    144)}};
  EXPECT_EQ(replyPoll(reply),
            "{poll=10,orders={"
            "{id=18,amount=1000,filter={name=\"minecraft:stick\",damage=0}},"
            "{id=19,amount=144,filter={name=\"ae2fc:fluid_drop\","
            "label=\"drop of Xenoxene\"}}},cancel={}}");
}

TEST(PollProtocol, ReplyCancelAndScan) {
  // The filter lets the agent check that the CPU still crafts our item: it
  // may not know the order (restarted), and the craft may have ended.
  auto order = sent(17, {"minecraft:stick", 0, ""}, "Stick", 1000);
  order.cpu = "CPU_022";
  const PollReply reply{
      .poll = 2s, .cancel = {order}, .scan = ScanTask{.id = 42, .params = {}}};
  EXPECT_EQ(replyPoll(reply),
            "{poll=2,orders={},cancel={{order=17,cpu=\"CPU_022\","
            "filter={name=\"minecraft:stick\",damage=0}}},"
            "scan={id=42,threshold=500,maxId=11500,chunk=1000,"
            "slow={from=250,to=320,chunk=5}}}");
}

}  // namespace
