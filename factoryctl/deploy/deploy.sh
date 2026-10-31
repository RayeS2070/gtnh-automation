#!/usr/bin/env bash
# Builds the image here and runs it on the game server's machine: nothing is compiled there.
#   deploy/deploy.sh [user@]host [ssh-port]
# or FACTORYCTL_HOST / FACTORYCTL_SSH_PORT; arguments win. A Host entry in ~/.ssh/config (user,
# port, key) works as is: deploy/deploy.sh myserver
set -euo pipefail

host=${1:-${FACTORYCTL_HOST:-}}
port=${2:-${FACTORYCTL_SSH_PORT:-}}
if [[ -z $host ]]; then
    echo "usage: $0 [user@]host [ssh-port]   (or FACTORYCTL_HOST, FACTORYCTL_SSH_PORT)" >&2
    exit 2
fi
ssh_opts=()
[[ -n $port ]] && ssh_opts=(-p "$port")
remote=factoryctl  # in the remote home
here=$(cd "$(dirname "$0")/.." && pwd)

on_host() { ssh "${ssh_opts[@]}" "$host" "$@"; }
rsync_to() { rsync -a -e "ssh ${ssh_opts[*]}" "$@"; }

docker build -t factoryctl:latest "$here"
# Natively and in a container, against the test database: nothing leaves if it fails.
"$here/deploy/check.sh"

on_host mkdir -p "$remote/data"
# Icons: the calculator's export, about 42 MB; rsync sends it only when it changed.
rsync_to --delete --exclude .git "$here/../assets/data/" "$host:$remote/data/"
rsync_to "$here/deploy/compose.yaml" "$host:$remote/compose.yaml"

echo "sending the image..."
docker save factoryctl:latest | gzip | on_host 'gunzip | docker load'
# up -d alone kept the old container: the tag moved to a new image, the service config did not.
on_host "cd $remote && docker compose up -d db && docker compose up -d --force-recreate server \
    && docker image prune -f --filter label=factoryctl=server && docker compose ps"
