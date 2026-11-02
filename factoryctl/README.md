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
- `pumps/`, `qft/`, `mining/` — the agents of the Space Elevator pump modules, of a Quantum Force
  Transformer and of the Space Elevator mining modules.

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

**Pumping agent** (`pumps/`, Lua on its own computer, next to the Space Elevator pump modules and
the small pumping ME network). Unlike the crafting agent it decides by itself, so the modules keep
the fluid levels without the server:

- every module runs on its own: a pulse of work allowed (0.1 s) starts one cycle of `batch`
  seconds; when it stops, the next one starts. No sleeps: one `event.pull` waits for whatever is
  due next (a pulse to end, a cycle to end, a poll);
- a cycle's slots (T1: 1 slot of 4 parallels, T2: 4 × 4, T3: 4 × 64) get the fluids below
  `threshold × target`, ranked by `1 − fill^priority`, a different fluid in each slot and on each
  module first; what running cycles bring counts as stock already;
- settings (targets, priorities, batch, threshold) come from the server and are saved on the
  computer; until it gave any, `pumps/config.lua` has the defaults, and they become the server's
  first settings. Cycles, modules and the pumping network's stock go to the server, for the web.

**QFT agent** (`qft/`, ex-qftctl, Lua on its own computer next to one Quantum Force Transformer).
It decides by itself too: recipes by the stock of products and raw materials, plasma focusing,
the safe stop before every switch. The server keeps each setup's settings (mode, targets,
weights; edited on the QFT tab) and its last report. See [qft/README.md](qft/README.md).

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
already); the Pumping tab (modules with their slots and progress; fluids with the pumping
network's stock, target, fill, what is in flight and what was pumped in 24 h; targets, batch and
threshold edited there); the QFT tab (each setup: what it runs, its recipes and outputs, the
mode and the targets or weights edited there); the Logs tab and each order's history (events: what our code tells on purpose, not the
server's whole log). Lists refresh every 5 s. Targets are fresh within a poll, other stock with the scans.
Item icons come from the GTNH Calculator's export (`../assets/data`, served at `/data/`): the
page reads `data.bin` the way the calculator does and draws icons from `atlas.webp`. The export
is of another game version, so a few goods have no icon.

**HTTP API**

| Path | |
|---|---|
| `POST /agent/v1/poll`, `POST /agent/v1/scan` | the agent; answers are Lua tables, always HTTP 200 with `ok` |
| `POST /agent/v1/pumps/poll` | the pumping agent: modules, cycles, stock up; newer settings down |
| `GET /api/v1/pumps`, `PUT /api/v1/pumps/settings`, `PUT|DELETE /api/v1/pumps/targets/{fluid}` | pumping: modules, fluids (stock, target, in flight, pumped in 24 h); edits raise the settings' version |
| `POST /agent/v1/qft/poll` | a QFT agent: its setup (name, controller coordinates), state, recipes, plasma up; newer settings down. A second QFT under a taken name is refused |
| `GET /api/v1/qft`, `PUT /api/v1/qft/{name}/settings`, `DELETE /api/v1/qft/{name}` | QFT setups: the last report, the recipes, the settings (mode; targets, priorities, weights); forget a setup |
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
curl localhost:8081/health
```

The web client: `cd web && npm install && npm run build` before starting the server, then open
http://localhost:8081 (see `web/README.md` for development). Icons need the `assets` submodule
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

The server keeps no accounts. People reach it through an SSH tunnel or through an HTTPS proxy
that asks for a password (below); behind the proxy, the server takes the person's name from
`X-Forwarded-User` for one-off orders and for the events of what they change.

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

Ports, the same wherever the server is deployed (`127.0.0.1` only):

| port  | what |
|-------|------|
| 8080  | the deployed server: agents (`server` in their configs), the HTTPS proxy, an SSH tunnel |
| 8443  | the HTTPS proxy with passwords (`deploy/nginx/factoryctl.conf`) |
| 8081  | the development server (`server/configs/config_vars.yaml`), `npm run dev` passes to it |
| 5173  | `npm run dev` |
| 15432 | the development PostgreSQL (`compose.yaml`), with `factoryctl_test` for the tests |
| 18081, 18091 | `deploy/check.sh`: the server natively and in a container, while it runs |

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

### HTTPS and passwords

`deploy/nginx/factoryctl.conf` is the nginx server block for it: TLS, `auth_basic` with a
password per person, the person's name passed on in `X-Forwarded-User`, `/agent/` closed (the
agents post to the server directly), and a day of cache for the icons. Passwords, one line per
person (nginx reads the file on every request, no reload; its workers, not root, read it: `0644`,
it holds hashes only):

```sh
printf 'pavel:%s\n' "$(openssl passwd -6)" >> factoryctl.htpasswd
```

The server believes the header, so it must be reachable through the proxy only: it listens on
`127.0.0.1` and the compose network, and nginx sets the header itself. `GET /api/v1/me` says who
the server sees (`{"user": null}` through a tunnel); the order form shows that name instead of
asking. With a self-signed certificate, add it to the browsers rather than click through the
warning: a password over a connection nobody checked can be read by whoever sits in between.

## Agent (in game)

Needs an Internet Card and an Adapter next to a fluid interface or the ME controller: on GTNH 2.9
only those answer for fluids (an ME interface still does for items). Install with the repository's
installer and set the server address in `agent/config.lua`:

```
gtnh-install factoryctl
cd /home/factoryctl/agent && edit config.lua && main
```

OpenComputers refuses private and local addresses by default (`internet.filteringRules` in its
config on the game server): allow the factoryctl server's address there.

Local tests with fake components built from real dumps: `cd agent && lua tests/run.lua`.

The pumping agent goes on a computer of its own, with an Internet Card and Adapters at the pump
modules and at the pumping network's fluid interface or ME controller:

```
gtnh-install factoryctl
cd /home/factoryctl/pumps && edit config.lua && main
```

Its local tests: `cd pumps && lua tests/run.lua`; against a running server, with fake modules and
simulated time: `lua tests/live.lua` (the development server, http://localhost:8081).

### Mining agent (`mining/`)

The Space Elevator mining modules. Unlike the other agents it does not decide: the server plans
every module, the agent carries the plan out and keeps its own safety.

**Targets** (the Mining tab): keep a product — a dust, a fluid — at a threshold, made of some
module outputs (ores). The tab suggests the ores from the mining catalog: what processing turns
into the product, with the asteroids that drop them; which ones count is the player's call (an
ore makes many dusts, a dust comes from many ores).

**The plan** (`server/src/domain/mining.*`, on smc's mechanics: `../smc/core`). A module draws its
asteroid at random among the candidates its drone and distance allow, every operation, so a
setting (module tier, drone tier, distance, plasma, parallels) is a mix of outputs. Its worth is
Σ ore demand × the ore's expected rate per second; the demand of an ore is the urgency
(`1 - fill^priority`) of the targets it feeds. Modules are planned greedily, the bigger first:
each takes the best setting it has a drone for, then discounts what it covers for the next; one
keeps its setting unless the best is 25 % better (a change costs a drone swap). The plasma is
the best one in stock (`plasma-minimum`); an ore with `buffer-limit` items in the buffer before
processing is not given to any module until processing catches up. The rest is voided: the
keeping storage bus gets the wanted ores only.

**Hardware, per module**: an Adapter on the controller (an MFU works), its ME stocking input bus
(drill tip and rod) and stocking input hatch (plasma) — both found by their coordinates, the
nearest to the controller —, a plain input bus with a transposer between it and an interface of
the drone pool: the module takes no drone from an ME bus and its controller slot allows no
automation. Drill tips, rods and plasma come from the main network, where they are ordinary
crafting targets. **Shared**: the drone pool network, the storage buses of the output network
(keep: into processing; void: an Inverter Card, the same list, into the trash — nothing wanted is
voided even when processing is full) and the buffer network.

```
gtnh-install factoryctl
cd /home/factoryctl && mining/tools/mining-dump.lua   # addresses, sides, coordinates
cd mining && edit config.lua && main
```

`config.lua`: the setup's name, per module (by its controller's coordinates) the transposer, its
two sides and the pool interface, the storage buses and the buffer network. Several computers
may share one server: each is a setup, the plan of one counts what the others mine.

Without the server the modules go on with their last setting; once a kept ore fills the buffer
the agent holds them until the buffer drains or the server is back. Ctrl+C: every module ends its
operation and stands.

Local tests: `cd mining && lua tests/run.lua`; against a running server, with a fake module and
simulated time: `lua tests/live.lua` (the development server). The mining catalog
(`server/data/mining_catalog.json`) comes from `mining/tools/mining-catalog.py`: smc's asteroids
(GT5U), the item ids of their outputs (`mining/data/asteroid_dump.json`, from MEDINA's
asteroiddump mod) and the modpack's labels, checked against each other.

Much of what the agent knows of the modules comes from
[MEDINA](https://github.com/jdlovins/GTNH-Miner) (GTNH-Miner by Josh Lovins and contributors), an
OpenComputers mining system for the same modules: the parameter names on 2.9, the drone path, the
pitfalls of loading, the asteroid dump. No code of it is used; the asteroid dump is copied as it
is (the repository is public domain).

