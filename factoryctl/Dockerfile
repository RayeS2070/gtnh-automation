# The factoryctl server with its web client, for the machine next to the game server.
# Build from factoryctl/: deploy/deploy.sh does it, see "Deploy" in README.md.

# --- the web client ------------------------------------------------------------------------------
FROM node:24-slim AS web
WORKDIR /web
COPY web/package.json web/package-lock.json ./
RUN --mount=type=cache,target=/root/.npm npm ci
COPY web/ ./
RUN npm run build

# --- the server ----------------------------------------------------------------------------------
# The same Ubuntu as the target machine: its toolchain builds C++23, and the libraries found by
# ldd below are the ones the runtime image has a glibc for. No libfmt-dev: Ubuntu's fmt does not
# compile with its clang (consteval in fmt/chrono.h); userver then fetches its own.
FROM ubuntu:26.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    rm -f /etc/apt/apt.conf.d/docker-clean && apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates git cmake ninja-build ccache clang clang-format lld pkg-config \
        python3 python3-venv python3-dev \
        libboost-dev libboost-context-dev libboost-coroutine-dev libboost-filesystem-dev \
        libboost-iostreams-dev libboost-locale-dev libboost-program-options-dev \
        libboost-stacktrace-dev \
        libc-ares-dev libcctz-dev libcrypto++-dev libcurl4-openssl-dev libdouble-conversion-dev \
        libev-dev libjemalloc-dev libnghttp2-dev libpq-dev libre2-dev libssl-dev \
        liburing-dev libxxhash-dev libyaml-cpp-dev libzstd-dev libbenchmark-dev \
        libgtest-dev libgmock-dev
WORKDIR /src
COPY CMakeLists.txt CMakePresets.json ./
COPY cmake/ cmake/
COPY server/ server/
# CPM keeps userver's sources in ~/.cache/CPM (CMakePresets.json), ccache its objects: a rebuild
# compiles only what changed.
RUN --mount=type=cache,target=/root/.cache \
    cmake --preset linux_clang_release \
    && cmake --build --preset linux_clang_release --target factoryctl_server \
    && mkdir -p /out/lib \
    && cp build/linux/linux_clang_release/server/factoryctl_server /out/ \
    && strip /out/factoryctl_server \
    && ldd /out/factoryctl_server | awk '/=> \// {print $3}' \
       | grep -v -E '/(libc|libm|libdl|libpthread|librt|ld-linux[^/]*)\.so' \
       | xargs -r cp -L -t /out/lib/

# --- what runs -----------------------------------------------------------------------------------
FROM ubuntu:26.04
# deploy.sh removes old images of factoryctl by it, and nothing else.
LABEL factoryctl=server
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /out/lib/ /app/lib/
COPY --from=build /out/factoryctl_server /app/
COPY --from=web /web/dist/ /app/web/
COPY server/configs/ /app/configs/
COPY deploy/config_vars.yaml /app/configs/deploy_vars.yaml
ENV LD_LIBRARY_PATH=/app/lib
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=3s CMD curl -sf http://localhost:8080/health || exit 1
CMD ["/app/factoryctl_server", "--config", "/app/configs/static_config.yaml", \
     "--config_vars", "/app/configs/config_vars.yaml", \
     "--config_vars_override", "/app/configs/deploy_vars.yaml"]
