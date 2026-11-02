#include <gmock/gmock.h>

#include <userver/dynamic_config/test_helpers.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/utest/utest.hpp>

#include "config/dynamic.hpp"

namespace factoryctl::config {
namespace {

namespace json = userver::formats::json;
using namespace std::chrono_literals;
using testing::ElementsAre;
using testing::HasSubstr;
using testing::UnorderedElementsAre;

auto poll(std::string_view text) -> domain::Settings {
  return parsePoll(json::FromString(text));
}

auto throwsWith(std::string_view text) -> std::string {
  try {
    poll(text);
  } catch (const std::exception& e) {
    return e.what();
  }
  return "no exception";
}

TEST(DynamicConfig, DefaultsAreTheDomainOnes) {
  const auto& snapshot = userver::dynamic_config::GetDefaultSnapshot();
  EXPECT_EQ(snapshot[kPoll], domain::Settings{});
  EXPECT_EQ(snapshot[kEvents], EventsSettings{});
}

TEST(DynamicConfig, EmptyPollIsTheDefaults) {
  EXPECT_EQ(poll("{}"), domain::Settings{});
}

TEST(DynamicConfig, PollFieldsOverTheDefaults) {
  const auto s = poll(R"({
    "poll-interval": "5s", "scan-every": "2m", "scan-give-up": "1h",
    "sent-timeout": "30s", "computing-timeout": "45s",
    "stalled-timeout": "20m", "low-free-cpus": 4, "scan-threshold": 100,
    "scan-max-id": 12000, "scan-chunk": 500, "scan-slow-from": 200,
    "scan-slow-to": 330, "scan-slow-chunk": 2
  })");
  EXPECT_EQ(s.poll, 5s);
  EXPECT_EQ(s.scanEvery, 2min);
  EXPECT_EQ(s.scanGiveUp, 1h);
  EXPECT_EQ(s.timeouts.sent, 30s);
  EXPECT_EQ(s.timeouts.computing, 45s);
  EXPECT_EQ(s.timeouts.stalled, 20min);
  EXPECT_EQ(s.lowFreeCpus, 4);
  EXPECT_EQ(s.scan.threshold, 100);
  EXPECT_EQ(s.scan.maxId, 12000);
  EXPECT_EQ(s.scan.chunk, 500);
  EXPECT_EQ(s.scan.slowFrom, 200);
  EXPECT_EQ(s.scan.slowTo, 330);
  EXPECT_EQ(s.scan.slowChunk, 2);
}

TEST(DynamicConfig, OneFieldLeavesTheOthers) {
  auto expected = domain::Settings{};
  expected.scanEvery = 2min;
  EXPECT_EQ(poll(R"({"scan-every": "2m"})"), expected);
}

TEST(DynamicConfig, BadPollValuesThrow) {
  EXPECT_THAT(throwsWith(R"({"scan-evry": "2m"})"), HasSubstr("scan-evry"));
  EXPECT_THAT(throwsWith(R"({"poll-interval": 10})"),
              HasSubstr("poll-interval"));
  EXPECT_THAT(throwsWith(R"({"poll-interval": "soon"})"),
              HasSubstr("poll-interval"));
  // Below a second the agent would hammer the server; a period of 0 is a typo.
  EXPECT_THAT(throwsWith(R"({"poll-interval": "500ms"})"),
              HasSubstr("poll-interval"));
  EXPECT_THAT(throwsWith(R"({"scan-every": "0s"})"), HasSubstr("scan-every"));
  EXPECT_THAT(throwsWith(R"({"low-free-cpus": -1})"),
              HasSubstr("low-free-cpus"));
  EXPECT_THAT(throwsWith(R"({"scan-chunk": 0})"), HasSubstr("scan-chunk"));
  EXPECT_THAT(throwsWith(R"({"scan-slow-chunk": "5"})"),
              HasSubstr("scan-slow-chunk"));
  EXPECT_THAT(throwsWith(R"({"scan-slow-from": 400})"),
              HasSubstr("scan-slow-from"));
  EXPECT_THAT(throwsWith(R"([1])"), HasSubstr("object"));
}

TEST(DynamicConfig, Mining) {
  EXPECT_EQ(parseMining(json::FromString("{}")), MiningSettings{});
  EXPECT_EQ(parseMining(json::FromString(R"({"buffer-limit": 5000})")).bufferLimit,
            5000);
  EXPECT_THROW(parseMining(json::FromString(R"({"buffer-limit": 0})")),
               std::runtime_error);
  EXPECT_THROW(parseMining(json::FromString(R"({"plasma-minimum": "1k"})")),
               std::runtime_error);
  EXPECT_EQ(keyOfField("buffer-limit"), "FACTORYCTL_MINING");
}

TEST(DynamicConfig, Events) {
  EXPECT_EQ(parseEvents(json::FromString("{}")), EventsSettings{});
  EXPECT_EQ(parseEvents(json::FromString(R"({"keep": "7d"})")).keep,
            std::chrono::days{7});
  EXPECT_THROW(parseEvents(json::FromString(R"({"keep": "0d"})")),
               std::exception);
  EXPECT_THROW(parseEvents(json::FromString(R"({"kep": "7d"})")),
               std::exception);
}

TEST(DynamicConfig, DocsFromYaml) {
  const auto docs = parseDocs(R"(
# comments are fine
FACTORYCTL_POLL:
  scan-every: 2m      # a period
  scan-chunk: 500
USERVER_LOG_REQUEST: false
)");
  EXPECT_THAT(docs.GetNames(),
              UnorderedElementsAre("FACTORYCTL_POLL", "USERVER_LOG_REQUEST"));
  // Numbers stay numbers, periods strings: the parser takes them as they are.
  const auto s = parsePoll(docs.Get("FACTORYCTL_POLL"));
  EXPECT_EQ(s.scanEvery, 2min);
  EXPECT_EQ(s.scan.chunk, 500);
  EXPECT_FALSE(docs.Get("USERVER_LOG_REQUEST").As<bool>());
}

TEST(DynamicConfig, EmptyDocs) {
  EXPECT_TRUE(parseDocs("").GetNames().empty());
  EXPECT_TRUE(parseDocs("# nothing yet\n").GetNames().empty());
}

TEST(DynamicConfig, BadDocsThrow) {
  EXPECT_THROW(parseDocs("- a list\n"), std::exception);
  EXPECT_THROW(parseDocs("FACTORYCTL_POLL: {scan-every: 2m\n"), std::exception);
}

TEST(DynamicConfig, KeyWithoutFieldsIsTheDefaults) {
  // As in the host's template: the key stays, its fields are commented out.
  const auto docs = parseDocs("FACTORYCTL_POLL:\n#  scan-every: 2m\n");
  EXPECT_EQ(parsePoll(docs.Get("FACTORYCTL_POLL")), domain::Settings{});
  EXPECT_EQ(parseEvents(json::FromString("null")), EventsSettings{});
}

TEST(DynamicConfig, KeyOfField) {
  EXPECT_EQ(keyOfField("scan-every"), "FACTORYCTL_POLL");
  EXPECT_EQ(keyOfField("scan-slow-chunk"), "FACTORYCTL_POLL");
  EXPECT_EQ(keyOfField("keep"), "FACTORYCTL_EVENTS");
  EXPECT_EQ(keyOfField("FACTORYCTL_POL"), std::nullopt);
}

TEST(DynamicConfig, UnknownNames) {
  const auto docs = parseDocs("FACTORYCTL_POLL: {}\nFACTORYCTL_POL: {}\n");
  EXPECT_THAT(unknownNames(docs, {"FACTORYCTL_POLL", "FACTORYCTL_EVENTS"}),
              ElementsAre("FACTORYCTL_POL"));
}

}  // namespace
}  // namespace factoryctl::config
