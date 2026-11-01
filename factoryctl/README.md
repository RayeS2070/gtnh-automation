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

Fluids: GTNH 2.8.4 keeps them in AE as ae2fc drops, found in the item scan; 2.9 keeps them as
fluids. The agent tells them apart by `getFluidInNetwork`, which 2.8.4 does not have, and on 2.9
scans fluids with one `getFluidsInNetwork()`, measures them with `getFluidInNetwork(name)` and
orders them by name. Both get the same key on the server (the drop's, by the fluid's internal
name), so stock, targets and icons do not depend on the version. A CPU finishing a fluid batch
bigger than asked shows a negative `finalOutput().size`: the surplus, shown as such.

Results and log lines stay with the agent until a poll gets through. Every order is requested
at most once: one whose result never comes is marked lost, never sent again by itself.

**Server** (`server/`, C++ with userver). One poll is one transaction:

- results first, then CPUs: orders move `queued → sent → computing/accepted → crafting → done`
  (or `failed`, `canceled`, `lost`); a craft that left its CPU is done;
- time limits: lost orders, and alarms in the log for long computing and stalled crafts;
- goods AE cannot craft (no pattern; from scans, target probes and failed orders) are not
  ordered: targets wait, one-off orders are refused; a craft whose pattern is lost on the way
  raises an alarm in the log (it may hang on its CPU; canceling is left to a person);
- targets: an order when the stock is below the threshold, one batch or, with *fill*, what is
  missing in whole batches; low priority only while enough CPUs are idle (`low-free-cpus`);
  goods whose order closed wait until their stock is measured again;
- at most one open order per goods (the database enforces it), automatic or one-off.

The schema is one file, `server/schema/schema.sql`, versioned; there are no migrations: a
server finding another version refuses to start and says how to drop the old schema.
Periods, timeouts and scan parameters are the dynamic config (`server/src/config/dynamic.hpp`):
a YAML file the server reads again every 5 s, so a change applies without a restart. Started
here, the server reads `dynamic_config.yaml` in the directory it starts from (not in git; no
file means the defaults); `deploy/host_dynamic_config.yaml` lists every key with its default.

**Web client** (`web/`, Svelte + TypeScript). Stock as a grid of tiles with search by words,
items or fluids, by name or amount; the chosen goods' target and order forms above it; targets (threshold, batch,
priority, fill, pause); one-off orders and cancel; the order list with status, CPU and
progress; the CPUs and what they craft (a tile and the order form say when goods are on a CPU
already); the Logs tab and each order's history (events: what our code tells on purpose, not the
server's whole log). Lists refresh every 5 s. Targets are fresh within a poll, other stock with the scans.
Item icons come from the GTNH Calculator's export (`../assets/data`, served at `/data/`): the
page reads `data.bin` the way the calculator does and draws icons from `atlas.webp`. The export
is of another game version, so a few goods have no icon.

**HTTP API**

| Path | |
|---|---|
| `POST /agent/v1/poll`, `POST /agent/v1/scan` | the agent; answers are Lua tables, always HTTP 200 with `ok` |
| `GET /api/v1/stock?q=&kind=&sort=&dir=&limit=` | stock: every word of `q` in the label, any order; `kind` all/items/fluids; `sort` label/amount; `dir` asc/desc; everything without `limit` |
| `GET /api/v1/cpus` | the CPUs of the agent's last poll: what each crafts, what is left, our open order of it; and `lowFreeCpus` |
| `GET /api/v1/events?level=&source=&q=&order=&before=&limit=` | events, newest first: the agent's lines, every order status change, alarms, scans, targets (kept `FACTORYCTL_EVENTS.keep`, 30 days) |
| `GET /api/v1/targets`, `PUT|DELETE /api/v1/targets/{goods_id}` | targets; `group` in the settings: a group's id or null |
| `GET|POST /api/v1/target-groups`, `PUT|DELETE /api/v1/target-groups/{group_id}` | groups of targets, for people only: list, add, rename, delete (its targets stay, ungrouped) |
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

Text answers (JSON, the web client) are compressed, zstd or gzip, as the browser asks; icons
are not (they are compressed already).

There is no authentication: reach the server through an SSH tunnel.

## Deploy

The server runs next to the game server, in Docker; nothing is compiled there.
`deploy/deploy.sh` builds the image here (`Dockerfile`: the web client in Node, the server on
Ubuntu 26.04 with the `linux_clang_release` preset; CPM and ccache caches make a rebuild quick),
copies the icons (`../assets/data`) and `deploy/compose.yaml` to `~/factoryctl` on the host,
checks it (`deploy/check.sh`: the server natively and the image in a local container, both against
the test database, must answer every page and log no errors; nothing is sent if they fail), sends
the image over SSH (`docker save | docker load`, no registry) and runs `docker compose up -d`:
`deploy/deploy.sh [user@]host [ssh-port]`, or `FACTORYCTL_HOST` / `FACTORYCTL_SSH_PORT`. A `Host`
entry in `~/.ssh/config` is the simplest: `deploy/deploy.sh myserver`.

On the host the server listens on `127.0.0.1:8080` only: the agent runs on the same machine
(`server = 'http://127.0.0.1:8080'` in `agent/config.lua`), people open the web client through
`ssh -L 8080:127.0.0.1:8080`. PostgreSQL is not published; its data is the `factoryctl_pgdata`
volume.

Values can be tuned on the host without a deploy, in `~/factoryctl/config/`. `deploy.sh` puts
both files there once, from `deploy/host_*`, and never overwrites them; set only what differs.

- `dynamic_config.yaml`: periods, timeouts, scan parameters, how long events are kept, and
  userver's own keys. Read again every 5 s, no restart. A file the server refuses changes
  nothing (the values before it stay); the log says why:

  ```sh
  docker compose logs server --no-log-prefix | jq -r 'select(.text | test("dynamic config")) | .text'
  ```

- `config_vars.yaml`: what is read at start only (log level, threads), over the image's
  `config_vars.yaml` (`server/configs/config_vars.yaml` with `deploy/config_vars.yaml` merged in).
  Apply with `docker compose restart server && docker compose ps`; a bad value stops the server
  until it is fixed.

Logs are JSON, one object per line (`logger-format` in `config_vars.yaml`; `tskv` and `raw` are
the other formats): `timestamp`, `level`, `module`, `text`, and tags. Lines the agent sent have
`"source": "agent"` and `agent_uptime`. Most lines are userver's records of each request; to read
the rest, in `~/factoryctl`:

```sh
docker compose logs server --no-log-prefix -f | jq -r 'select(.text != "") |
  "\(.timestamp[11:19]) \(.level) \(if .source == "agent" then "[agent] " else "" end)\(.text)"'
docker compose logs server --no-log-prefix | jq -c 'select(.level == "ERROR" or .level == "WARNING")'
```

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
