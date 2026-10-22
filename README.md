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
