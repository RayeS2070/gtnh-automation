#!/usr/bin/env bash
# Builds the image here and runs it on the game server's machine: nothing is compiled there.
#   deploy/deploy.sh                 host and port below, or FACTORYCTL_HOST / FACTORYCTL_SSH_PORT
set -euo pipefail

host=${FACTORYCTL_HOST:-Rayes@gtnh}
port=${FACTORYCTL_SSH_PORT:-2424}
remote=factoryctl  # in the remote home
here=$(cd "$(dirname "$0")/.." && pwd)

on_host() { ssh -p "$port" "$host" "$@"; }

docker build -t factoryctl:latest "$here"

on_host mkdir -p "$remote/data"
# Icons: the calculator's export, about 42 MB; rsync sends it only when it changed.
rsync -a --delete --exclude .git -e "ssh -p $port" "$here/../assets/data/" "$host:$remote/data/"
rsync -a -e "ssh -p $port" "$here/deploy/compose.yaml" "$host:$remote/compose.yaml"

echo "sending the image..."
docker save factoryctl:latest | gzip | on_host 'gunzip | docker load'
on_host "cd $remote && docker compose up -d && docker compose ps"
