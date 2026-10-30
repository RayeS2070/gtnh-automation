# factoryctl

Remote production control for GregTech: New Horizons: keep stock above thresholds with AE2
autocrafting, order batches from outside the game, watch the stock.

```
agent (OpenComputers, in game) --HTTP poll--> server (C++, userver) --> PostgreSQL
                                                 ^
                                  web page served by the server
```

- `agent/` — OC program in the game: scans the ME network, places and cancels crafting orders.
- `server/` — the server next to the game server: stock, thresholds, order journal.
- `web/` — the web client (Svelte + TypeScript), built into static files the server serves.

Replaces `../maintainer/` and, for items and fluids, the InfluxDB exporter.

## How it works

**Agent** (`agent/`, Lua on an OpenComputers computer). It decides nothing; every poll it:

- reports the crafting CPUs (`finalOutput()` of each: what it crafts and what is left), the
  results of its orders, the stock of the targets the server listed (point lookups,
  `getItemInNetwork`, measured after the CPUs) and its own log lines;
- runs what the reply hands out: orders (`getCraftables(filter)`, `request`), cancels (only if
  the CPU still shows that item) and scans of the whole network (`getItemsInNetworkById` in id
  chunks; ids 250..320 in chunks of 5, or the game server runs out of memory);
- polls every `poll-interval`, and at once when one of its crafts ends (it looks at its own jobs
  every second; on GTNH 2.9 network events can take the place of that).

Results and log lines stay with the agent until a poll gets through. Every order is requested
at most once: one whose result never comes is marked lost, never sent again by itself.

**Server** (`server/`, C++ with userver). One poll is one transaction:

- results first, then CPUs: orders move `queued → sent → computing/accepted → crafting → done`
  (or `failed`, `canceled`, `lost`); a craft that left its CPU is done;
- time limits: lost orders, and alarms in the log for long computing and stalled crafts;
- targets: an order when the stock is below the threshold, one batch or, with *fill*, what is
  missing in whole batches; low priority only while enough CPUs are idle (`low-free-cpus`);
  goods whose order closed wait until their stock is measured again;
- at most one open order per goods (the database enforces it), automatic or one-off.

The schema is one file, `server/schema/schema.sql`, versioned; there are no migrations: a
server finding another version refuses to start and says how to drop the old schema.
Periods and scan parameters are in `server/configs/config_vars.yaml`.

**Web client** (`web/`, Svelte + TypeScript). Stock as a grid of tiles with search by words,
items or fluids, by name or amount; the chosen goods' target and order forms above it; targets (threshold, batch,
priority, fill, pause); one-off orders and cancel; the order list with status, CPU and
progress. Lists refresh every 5 s. Targets are fresh within a poll, other stock with the scans.
Item icons come from the GTNH Calculator's export (`../assets/data`, served at `/data/`): the
page reads `data.bin` the way the calculator does and draws icons from `atlas.webp`. The export
is of another game version, so a few goods have no icon.

**HTTP API**

| Path | |
|---|---|
| `POST /agent/v1/poll`, `POST /agent/v1/scan` | the agent; answers are Lua tables, always HTTP 200 with `ok` |
| `GET /api/v1/stock?q=&kind=&sort=&dir=&limit=` | stock: every word of `q` in the label, any order; `kind` all/items/fluids; `sort` label/amount; `dir` asc/desc |
| `GET /api/v1/targets`, `PUT|DELETE /api/v1/targets/{goods_id}` | targets |
| `GET|POST /api/v1/orders`, `POST /api/v1/orders/{order_id}/cancel` | orders |
| `GET /health`, `GET /ping` | |

## Build

Needs CMake 3.30+, Ninja, clang or gcc, and system packages for userver: boost, c-ares, curl,
fmt, jemalloc, libev, libnghttp2, openssl, postgresql-libs, yaml-cpp, zlib, python; from AUR
libbacktrace-git and boost-stacktrace-backtrace for stack traces (optional). userver 3.2
and what the system lacks come through CPM.

```sh
docker compose up -d     # development database on localhost:15432
cmake --preset linux_clang_debug && cmake --build --preset linux_clang_debug
build/linux/linux_clang_debug/server/factoryctl_server \
    --config server/configs/static_config.yaml --config_vars server/configs/config_vars.yaml
curl localhost:8080/health
```

The web client: `cd web && npm install && npm run build` before starting the server, then open
http://localhost:8080 (see `web/README.md` for development). Icons need the `assets` submodule
with its `data` submodule checked out (`data-dir` in `config_vars.yaml`).

Tests:

```sh
ctest --test-dir build/linux/linux_clang_debug            # all; database tests need compose up
ctest --test-dir build/linux/linux_clang_debug -LE db     # without the database
```

The database tests use their own database `factoryctl_test` (made by `initdb/` on the first
start of the volume) and drop the schema there all the time.

`FACTORYCTL_DB` overrides the database connection string of `config_vars.yaml`.

There is no authentication: reach the server through an SSH tunnel.

## Agent (in game)

Needs an Internet Card and an Adapter next to an ME interface. Install with the repository's
installer and set the server address in `agent/config.lua`:

```
gtnh-install factoryctl
cd /home/factoryctl/agent && edit config.lua && main
```

OpenComputers refuses private and local addresses by default (`internet.filteringRules` in its
config on the game server): allow the factoryctl server's address there.

Local tests with fake components built from real dumps: `cd agent && lua tests/run.lua`.
