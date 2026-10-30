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
http://localhost:8080 (see `web/README.md` for development).

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
