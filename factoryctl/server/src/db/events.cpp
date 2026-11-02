#include "db/events.hpp"

#include <format>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/io/chrono.hpp>
#include <userver/storages/postgres/transaction.hpp>

namespace factoryctl::db {

auto byWho(std::string_view user) -> std::string {
  return user.empty() ? std::string{} : std::format(" (by {})", user);
}

namespace {

constexpr auto kMaster = pg::ClusterHostType::kMaster;

}  // namespace

auto addEvents(pg::Transaction& tx, std::span<const Event> events) -> void {
  for (const auto& e : events) {
    const auto& k = e.goods;
    tx.Execute(
        "INSERT INTO factoryctl.events"
        "  (at, level, source, text, order_id, goods_id, agent_uptime) "
        "VALUES ($1, $2::factoryctl.event_level, $3, $4, $5,"
        "  CASE WHEN $11::bigint IS NOT NULL THEN $11"
        "       WHEN $6 THEN (SELECT id FROM factoryctl.goods"
        "                     WHERE kind = 'item' AND name = $7"
        "                       AND damage = $8 AND variant = $9)"
        "       ELSE (SELECT goods_id FROM factoryctl.orders WHERE id = $5)"
        "  END, $10)",
        pg::TimePointTz{e.at}, e.level, e.source, e.text, e.order,
        k.has_value(), k ? k->name : std::string(), k ? k->damage : 0,
        k ? k->variant : std::string(), e.agentUptime, e.goodsId);
  }
}

auto listEvents(pg::Cluster& db, const EventQuery& q) -> std::vector<EventRow> {
  // strpos, not LIKE: % and _ in the text are just characters.
  const auto rows = db.Execute(
      kMaster,
      "SELECT e.id, e.at, e.level::text AS level, e.source, e.text,"
      "       e.order_id, e.agent_uptime,"
      "       g.id AS goods_id, g.name, g.damage, g.variant, g.label "
      "FROM factoryctl.events e "
      "LEFT JOIN factoryctl.goods g ON g.id = e.goods_id "
      "WHERE e.level >= $1::factoryctl.event_level"
      "  AND ($2 = '' OR e.source = $2)"
      "  AND ($3 = '' OR strpos(lower(e.text), lower($3)) > 0)"
      "  AND ($4::bigint IS NULL OR e.order_id = $4)"
      "  AND ($5::bigint IS NULL OR e.id < $5) "
      "ORDER BY e.id DESC LIMIT $6",
      q.minLevel, q.source, q.text, q.order, q.before, q.limit);
  std::vector<EventRow> out;
  for (const auto& row : rows) {
    EventRow e;
    e.id = row["id"].As<std::int64_t>();
    e.at = row["at"].As<pg::TimePointTz>().GetUnderlying();
    e.level = row["level"].As<std::string>();
    e.source = row["source"].As<std::string>();
    e.text = row["text"].As<std::string>();
    e.order = row["order_id"].As<std::optional<std::int64_t>>();
    e.agentUptime = row["agent_uptime"].As<std::optional<double>>();
    if (const auto id = row["goods_id"].As<std::optional<std::int64_t>>()) {
      e.goods = GoodsRef{*id,
                         {row["name"].As<std::string>(),
                          row["damage"].As<std::int32_t>(),
                          row["variant"].As<std::string>()},
                         row["label"].As<std::string>()};
    }
    out.push_back(std::move(e));
  }
  return out;
}

auto deleteEventsBefore(pg::Cluster& db, domain::TimePoint before)
    -> std::int64_t {
  return static_cast<std::int64_t>(
      db.Execute(kMaster, "DELETE FROM factoryctl.events WHERE at < $1",
                 pg::TimePointTz{before})
          .RowsAffected());
}

}  // namespace factoryctl::db
