#!/usr/bin/env bash
# Starts the server natively and the image (factoryctl:latest) in a container, both against the
# test database, and checks that they answer and log no errors. Unit tests do not start the
# components: a bad static config (once, a comma in a schema) only shows here.
#   deploy/check.sh          deploy.sh runs it before sending anything to the host
set -euo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)
db=${FACTORYCTL_TEST_DB:-postgresql://factoryctl:factoryctl@127.0.0.1:15432/factoryctl_test}
native_port=18081
image_port=18091
tmp=$(mktemp -d)
pid=
container=

cleanup() {
    [[ -n $pid ]] && kill "$pid" 2>/dev/null || true
    [[ -n $container ]] && docker rm -f "$container" >/dev/null 2>&1 || true
    rm -rf "$tmp"
}
trap cleanup EXIT

fail() {
    echo "check: $*" >&2
    exit 1
}

# Waits for the server on $1, then asks every page. $2 prints its log.
probe() {
    local port=$1 log=$2 ok=
    for _ in $(seq 120); do
        if curl -sf "localhost:$port/health" >/dev/null; then ok=1; break; fi
        sleep 0.5
    done
    [[ -n $ok ]] || { $log 2>&1 | tail -5 >&2; fail "no answer on :$port"; }
    for path in / /health /api/v1/stock /api/v1/targets /api/v1/target-groups /api/v1/orders \
        /api/v1/cpus /api/v1/events /api/v1/pumps /api/v1/qft /api/v1/mining /api/v1/me \
        /data/data.bin; do
        local code
        code=$(curl -s -o /dev/null -w '%{http_code}' "localhost:$port$path")
        [[ $code == 200 ]] || fail ":$port$path answered $code"
    done
    # A mining poll goes through the whole plan (a new setup: no status before it).
    local reply
    reply=$(curl -s -X POST -H 'Content-Type: application/json' "localhost:$port/agent/v1/mining/poll" \
        --data-binary '{"setup": "check", "run": "r", "pool": [{"tier": 5, "count": 1}],
            "modules": [{"x": 1, "y": 2, "z": 3, "tier": 2, "state": "idle"}]}')
    [[ $reply == *poll=* ]] || fail ":$port mining poll answered '${reply:0:200}'"
    curl -s -o /dev/null -X DELETE "localhost:$port/api/v1/mining/setups/check"
    # The web client's bundle comes compressed to a browser that takes it.
    local js encoding
    js=$(curl -s "localhost:$port/" | grep -oE '/assets/[^"]+\.js' | head -1)
    encoding=$(curl -s -o /dev/null -D - -H 'Accept-Encoding: gzip, zstd' "localhost:$port$js" \
        | tr -d '\r' | awk -F': ' 'tolower($1) == "content-encoding" {print $2}')
    [[ $encoding == zstd ]] || fail ":$port$js is not compressed (Content-Encoding: '$encoding')"
    # After the pages: errors they caused count too.
    $log > "$tmp/log" 2>&1 || true
    if grep -q '"level":"ERROR"' "$tmp/log"; then
        grep '"level":"ERROR"' "$tmp/log" | head -3 >&2
        fail "errors in the log of :$port"
    fi
    grep -q 'dynamic config from [^"]*: FACTORYCTL_EVENTS, FACTORYCTL_MINING, FACTORYCTL_POLL' "$tmp/log" \
        || fail ":$port did not take the dynamic config"
}

# Every field of the dynamic config, at its default: a parser that refuses one shows here.
cat > "$tmp/dynamic_config.yaml" <<'YAML'
FACTORYCTL_POLL:
  poll-interval: 10s
  scan-every: 5m
  scan-give-up: 10m
  sent-timeout: 2m
  computing-timeout: 2m
  stalled-timeout: 10m
  low-free-cpus: 8
  scan-threshold: 500
  scan-max-id: 12000
  scan-chunk: 1000
  scan-slow-from: 250
  scan-slow-to: 320
  scan-slow-chunk: 5
FACTORYCTL_EVENTS:
  keep: 30d
FACTORYCTL_MINING:
  buffer-limit: 100000
  plasma-minimum: 100000
  drill-threshold: 16384
  drill-batch: 4096
  plasma: ''
  plasma-threshold: 1000000
  plasma-batch: 100000
YAML

native_log() { cat "$tmp/native.log"; }
image_log() { docker logs "$container"; }

echo "check: native"
(cd "$here" && cmake --build --preset linux_clang_debug --target factoryctl_server >/dev/null)
printf 'server-port: %s\ndb-connection: %s\ndynamic-config-path: %s\n' \
    "$native_port" "$db" "$tmp/dynamic_config.yaml" > "$tmp/native.yaml"
(cd "$here" && exec build/linux/linux_clang_debug/server/factoryctl_server \
    --config server/configs/static_config.yaml --config_vars server/configs/config_vars.yaml \
    --config_vars_override "$tmp/native.yaml") > "$tmp/native.log" 2>&1 &
pid=$!
probe "$native_port" native_log
kill "$pid"
pid=

echo "check: container"
# As on the host: the override is the mounted config/config_vars.yaml.
mkdir "$tmp/config"
printf 'server-port: %s\ndb-connection: %s\n' "$image_port" "$db" > "$tmp/config/config_vars.yaml"
cp "$tmp/dynamic_config.yaml" "$tmp/config/"
# Not --rm: a container that dies at start keeps its log for probe to show.
container=$(docker run -d --network host -v "$tmp/config:/app/config:ro" \
    -v "$here/../assets/data:/app/data:ro" factoryctl:latest)
probe "$image_port" image_log

echo "check: ok"
