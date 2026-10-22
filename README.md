# gtnh-automation

[OpenComputers](https://github.com/GTNewHorizons/OpenComputers) programs for the
**GregTech: New Horizons** modpack.

| Project | What it does |
| --- | --- |
| [qftctl](qftctl/) | Controls a Quantum Force Transformer: picks recipes by stock levels, manages Neptunium/Fermium plasma focusing. |
| [nanoresinctl](nanoresinctl/) | Keeps molten rare-earth metals stocked in an ME network by switching bundled redstone colors. |
| [pumpingctl](pumpingctl/) | Space Elevator pump module control (third-party script, being reworked). **Does not run yet**, see below. |
| [maintainer](maintainer/) | Work in progress: keeping item stock via AE2 crafting with an external checker service. |

Each project has its own README with installation and usage.

## Installing in game

The computer needs an **Internet Card**, and the server must allow HTTP in the OpenComputers
config (it does by default). Copy [tools/gtnh-install.lua](tools/gtnh-install.lua) once to
`/home/bin/gtnh-install.lua` on the computer: `/home/bin` is on the OpenOS `PATH`. It is called
`gtnh-install`, because `install` is a built-in OpenOS command.

### Private repository

While the repository is private, GitHub needs a token. Create a fine-grained personal access
token with access to this repository only and the single permission **Contents: Read-only**.
Put it on the computer as the only line of `/etc/gtnh-install.token` (`edit` the file and paste
with Insert). The installer sends it with every request.

Anyone who can open this computer can read the token, so give it nothing beyond read access
and a short expiration.

### Usage

Install or update any project that has a `manifest.lua`:

| Command | What it does |
| --- | --- |
| `gtnh-install <project>` | Installs into `/home/<project>`, or updates an existing installation. |
| `gtnh-install <project> <dir>` | Installs into another directory. |
| `gtnh-install <project> --config` | Also overwrites the project's config files with the ones from the repository. |
| `gtnh-install <project> --branch=<name>` | Downloads from another branch instead of `devel`. |

The program code is always replaced. Config files are downloaded only when they are missing,
so an update keeps your edits unless you pass `--config`. If a kept file differs from the
repository version, the installer says so — check what changed upstream before replacing it.

GitHub caches downloaded files for a few minutes, so an update right after a push may still
fetch the old version.

Each project lists its files in `<project>/manifest.lua`: `code` (replaced on every install),
`config` (kept) and `run` (the command printed at the end). A new `.lua` file must be added to
the manifest, or it will not be installed. Check locally with `lua tools/check-manifests.lua`.

## Known issues

- **pumpingctl does not run.** The script reads `threshold`, `maxBatchSize` and `color` as
  globals, but they are declared inside `const`, so the first fluid check fails with an
  arithmetic error on `nil`. More problems are listed for the rework; the script is not
  fixed yet.
- **maintainer has unresolved licensing.** It was imported from another repository and has
  several authors besides the owner of this repository. Their permission to publish the code
  under the Apache License 2.0 has not been obtained. Sort this out before the repository
  becomes public.

## Development

Clone with submodules:

```sh
git clone --recurse-submodules https://github.com/RayeS2070/gtnh-automation.git
# or, in an existing clone:
git submodule update --init
```

Submodules (read-only references, not edited here):

- `gtnh/` — [GTNH-OC-Lua-Documentation](https://github.com/Navatusein/GTNH-OC-Lua-Documentation):
  LuaLS type stubs for OpenComputers and GTNH components, plus real component dumps from the game.
  It targets GTNH 2.9.
- `assets/` — [GTNH Calculator](https://github.com/ShadowTheAge/gtnh): recipe data of the modpack.

`.luarc.json` points the Lua language server (the VS Code `Lua` extension by sumneko) at
`gtnh/lua`, so autocomplete for `component`, `event` and component proxies works in every
project. Open the repository root as the workspace.

The programs run on Lua 5.3 inside OpenOS. Locally you can only check syntax and run pure logic:

```sh
luac -p qftctl/main.lua
cd qftctl && lua tests/run.lua
```

## License

[Apache License 2.0](LICENSE)
