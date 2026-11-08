# QFT agent (qftctl)

An OpenComputers program that controls a **Quantum Force Transformer** (QFT) in the
GregTech: New Horizons modpack.

> **Status:** moved to GTNH 2.9: one stocking input bus and one hatch serve every recipe, the
> program sets their slots. Written and tested locally. See
> [Not verified in game yet](#not-verified-in-game-yet).

## The factoryctl server

The server is required: `server` in `config/components.lua` (the default: the address the
other factoryctl agents use), and an Internet Card. The agent reports to it every 10 seconds
and takes its settings: the mode and, per recipe, its trigger (maintaining products or
processing raw materials), whether its inputs are counted, which recipes may run at all
(Off/On on the tab: an off recipe never runs, a running one finishes its cycle and yields) and,
per output, the target and priority or the weight. They are edited on the **QFT** tab of the web
client and kept on this computer (`/etc/qftctl.settings`), so the QFT goes on with them when the
server is away, also after a restart. A recipe the server has no settings for yet is a
maintaining one that tracks nothing: it never runs until it is set on the tab. What a recipe is
made of comes from `qft/catalog.lua`, which recipes the QFT has from `config/catalysts.lua`. `--mode` on the
command line wins over the server's mode.

Each QFT has its own computer and its own name on the server: `name` in
`config/components.lua`, or `qft@x,y,z` of the controller when it is not set. A second QFT
reporting under a name another QFT (at other coordinates) holds is refused until it gets its own
name, or the other one is forgotten on the web page.

## Why setup is needed

The AE2 API gives a program no way to tell two ME networks apart: a fluid interface of the raw
materials network and one of the products network look exactly the same. The program still has
to know which is which, and which stocking hatch holds which plasma. So you tell it once, in
**setup mode**, and it remembers the component addresses.

## Requirements

- GTNH 2.9 (its stocking buses and hatches can be set from OpenComputers).
- An OpenComputers computer running OpenOS, with the Lua 5.3 architecture (the default).
- An Adapter next to every block the program talks to:
  - a fluid interface (or the ME controller) of the **raw materials** network and one of the
    **products** network: on 2.9 only these answer for fluids;
  - the Quantum Force Transformer controller;
  - **one** ME stocking input bus and **one** ME stocking input hatch on the QFT, basic or
    advanced, for every recipe: the program sets their slots to the inputs of the recipe it
    starts. Take any other stocking bus or hatch with recipe inputs off the QFT, or it feeds
    every recipe;
  - one more ME stocking input hatch for plasma: the program sets it to the plasma a recipe
    uses (Fermium, Neptunium, both or none) and enables it only then.

## Installation

Install with the [repository installer](../README.md#installing-in-game):

```
gtnh-install factoryctl
```

This installs into `/home/qftctl`, or updates an existing installation. `gtnh-install factoryctl --config`
also overwrites the files in `config/` with the ones from the repository.

The program code is always replaced. The files in `config/` are downloaded only when they are
missing: they hold your setup, so an update does not touch them unless you pass `--config`.
If a kept file differs from the repository version, the installer says so
(`kept config/components.lua (DIFFERS from the repository, ...)`) — check what changed
upstream before replacing it, your own edits would be lost.
Saved component addresses (`/etc/qftctl.bindings`) are never touched by the installer.

Without an Internet Card, copy `main.lua`, `config/` and `qft/` into one directory by hand.

`tools/qft-dump.lua` writes what the program sees (stocking buses and hatches with their slots,
the controller, a few stacks of each network) into `/home/qft-dump.txt`; it changes nothing.

Run the program **from its directory** (`cd /home/factoryctl/qft`). It takes the modules shared
with the other agents from `../agent`.

## Usage

| Command | What it does |
| --- | --- |
| `main setup` | Binds every component that is not bound yet, or whose saved address is gone. Already bound components are kept. |
| `main setup --all` | Binds every component again from scratch, ignoring saved addresses. |
| `main` | Normal mode (`--mode=auto`): maintains products first and processes raw materials when nothing is short. Checks the configuration and the saved addresses (if something is not bound, lists it and asks you to run `main setup`), then runs the QFT until Ctrl+C. |
| `main --mode=deficit` | Only maintains products: surplus recipes never run. |
| `main --mode=surplus` | Only processes raw materials: deficit recipes never run, a running surplus recipe never yields. |
| `main --debug` | Normal mode with per-tick output: the fill level of every tracked output and the cycles of raw materials in stock. |

### How normal mode works

Every 10 seconds (every second while the QFT finishes a cycle before a switch) the program
reads the products and raw materials networks and decides which recipe should run. Each recipe has a `trigger`, and the program works in one of two modes:

**Maintaining products** (`trigger = 'deficit'`) comes first:

- A recipe **starts** when one of its tracked outputs fell below 80% of its `target`.
- A running recipe **keeps going** until every tracked output reaches its `target`. Without
  this gap the QFT would switch on and off around the target on every poll.
- If several recipes are low, the most urgent one wins. Urgency of an output is
  `1 - (amount / target) ^ priority`.
- If some of its `inputs` have an `amount`, it needs raw materials for one cycle to start and to
  keep going.

**Processing raw materials** (`trigger = 'surplus'`) takes the machine while no product is short:

- A recipe **starts** when its inputs in the raw materials network cover 2 cycles, and
  **stops** when less than 1 cycle is left. A cycle takes `amount × catalysts` of every input;
  the scarcest input decides.
- If several recipes can start, the one with the most cycles in stock wins. A running one
  hands the machine to another surplus recipe only when that one has twice as many cycles:
  each switch costs a stop.
- As soon as some product is short, the running surplus recipe finishes its cycle and the
  machine switches to maintaining products.

If nothing is needed, the QFT stops. The log says when the mode changes.

Switching is safe for plasma. The program stops the controller (`setWorkAllowed(false)`: the
current cycle finishes, no new one starts) and waits until the machine is inactive. Only
then does it switch the inputs off, set the slots of the stocking bus and hatch to the next
recipe and the plasma hatch to its plasma (only the slots that differ), put the Neptunium
circuit into the bus and start again. On start the program first
stops the machine and switches every input off. On Ctrl+C it stops the controller and leaves
the inputs as they are.

**Fermium Plasma.** A recipe runs with Fermium when both hold:

- the Shielding Core tier is not below the recipe tier (otherwise plasma is spent for nothing);
- the source network holds enough plasma. The use rate is `4 × shieldingTier × √catalysts`
  L/s (per the GTNH wiki). To start, the stock must cover 600 s. While running it must cover
  the current cycle (read from the controller) plus 60 s.

When the stock gets too low, the recipe finishes its cycle and restarts without Fermium. The
QFT never runs dry mid-cycle: plasma that runs out mid-cycle spoils the cycle's outputs.
**Neptunium Plasma.** Neptunium moves probability between outputs, it adds none, so it
pays only when the outputs are needed unequally. For every option (no focus, focus on each
output) the program computes `Σ weight × chance`, with the chances ported from the QFT source.
The weight of an output is its urgency when maintaining products, and its `weight` from the
config when processing raw materials.
Neptunium goes on when the best focus beats no focus by 10% and the stock is enough (same
rule as Fermium). It stays on while the focus still beats no focus by 5%. The focus moves
to another output when that output beats it by 10%.

- On GTNH 2.9 Neptunium burns no plasma without a circuit (on 2.8.4 it did). The circuit is
  still set on the stocking bus **before** the hatches are enabled, and reset to `-1` only
  after every hatch is off.
- Moving the focus only changes the circuit, without stopping the machine. Switching
  Neptunium on or off goes through a stop after the cycle, like Fermium.

Saved addresses live in `/etc/qftctl.bindings`. Deleting that file is the same as
`main setup --all`.

## Setup step by step

1. **Configuration check.** Before anything else the program checks that every recipe in
   `config/catalysts.lua` is a catalyst of the catalog and that the values make sense (see
   [Configuration](#configuration)). If not, it prints the problems and stops.
2. **Walk over the components.** Setup goes over every component in `config/components.lua`
   in alphabetical order of its path (`network.production`, `network.source`,
   `bus`, `controller`, `hatch`, `network.production`, ...). A component that is already bound and still present prints `ok` and
   is skipped; with `--all` every saved address is forgotten and everything is bound again.
3. **Found by itself.** The stocking bus and the two stocking hatches (`bus`, `hatch`,
   `plasma.hatch`) all stock from the raw materials network, so any hatch can serve as either.
   When the computer sees exactly one stocking bus and two stocking hatches that no other
   component holds, setup binds them on its own and prints `found`; which hatch gets which role
   does not matter. If it sees more or fewer, it says how many it found and asks for them as below.
4. **Binding a component** — the program prints what it needs, for example:
   ```
   [network.source] Fluid interface of the RAW MATERIALS network
     Connect the fluid_interface or me_controller now, or connect it and press Enter. [s] skip.
   ```
   Then **connect that block** to the computer: place the adapter next to it, or break and
   place the adapter back if it is already there. Either:
   - the program notices the new component by itself and binds it right away, or
   - press **Enter** after connecting it: the program compares the list of components with the
     one from before the prompt and takes the single new one.

   Other keys while waiting:
   - **`s`** — skip this component, it stays unbound;
   - **Ctrl+C** — stop setup. Everything bound so far is kept.
5. **Saving.** Each address is written to `/etc/qftctl.bindings` as soon as it is bound, so
   stopping or crashing halfway loses nothing.
6. **Result.** At the end setup lists the components left unbound, if any. Run `main setup`
   again to finish them.

Safety checks during setup:

- one block cannot be bound to two components — for example, the same fluid interface as both
  raw materials and products. Setup says which component already holds it and keeps waiting;
- a component of the wrong type is ignored — a fluid interface will not be bound where a stocking
  hatch is expected;
- if several new components of the right type show up at once, setup does not guess and asks
  you to connect them one at a time.

## Configuration

The configuration is two files. You edit them by hand; the program only reads them.

| File | What it holds |
| --- | --- |
| `config/components.lua` | Which components the setup needs and how to find them; the server, the name, Fluid Mode. |
| `config/catalysts.lua` | Which catalysts are in the bulk catalyst housing, and how many: the recipes this QFT runs. |

### Components

```lua
network = {
    source = bind.manual{ type = { 'fluid_interface', 'me_controller' },
        prompt = 'Fluid interface of the RAW MATERIALS network' },
},
bus = bind.manual{ type = 'me_inputbus', prompt = 'ME stocking input bus of the QFT (items)' },
hatch = bind.manual{ type = 'me_inputhatch', prompt = 'ME stocking input hatch of the QFT (fluids)' },
```

- `bind.manual{ type, prompt }` — a component bound in setup mode; `prompt` is what the
  player sees while setup waits for it.
- `type` is the OpenComputers component type, or a list of types any of which will do. The QFT
  controller is `gt_machine`, ME stocking buses and hatches are `me_inputbus` and
  `me_inputhatch`.
- `bind.manual` accepts only `type` and `prompt`. Data about a component goes next to it, not
  into it: `plasma.neptunium = { hatch = bind.manual{...}, fluid = 'Neptunium Plasma' }`.
- `shieldingTier` — the tier of the Shielding Core Casings (1..4).
- `bus`, `hatch` — the one stocking input bus and hatch of every recipe; the bus also holds the
  programmed circuit for Neptunium focusing. The old per-recipe `recipes = { ... }` table is
  refused with a hint: the inputs come from `qft/catalog.lua`.

### Recipes

What a recipe takes and gives is not typed in: `qft/catalog.lua` holds every QFT recipe of the
modpack (inputs with their internal names, outputs in NEI order, the focus tier, the catalyst),
generated by `tools/qft-recipes.py` from the GTNH calculator's data (run it again when the
modpack moves on; recipes with an ore dictionary input are left out: a bus slot takes one item).
The calculator's data has no catalysts: the script has them in a table taken from the mods'
sources and stops when a recipe has none.

> **The calculator's data cannot be fully trusted.** It lags the modpack: Rubber Polymer has
> Carbon 64 there, while GT5-Unofficial on the modpack's tag has Sulfur 32 + Carbon 32. The
> inputs and outputs of the catalog are not checked against the mods' sources; before a recipe
> is trusted, compare it with NEI in game. A wrong input leaves the stocking bus without it and
> the recipe does not start.

`config/catalysts.lua` lists every catalyst, commented out; uncomment the ones in the bulk
catalyst housing and give how many, the parallels. Every recipe of a listed catalyst runs on this
QFT (`0`: the recipes are there but never run):

```lua
return {
    ['Platinum Group Catalyst'] = 1,      -- 8 recipes: Platinum, Palladium, Osmium, ...
    ['Simple Naquadah Catalyst'] = 16,    -- Inert Naquadah Dust, Inert Enriched Naquadah Dust
    -- ['Rare-Earth Group Catalyst'] = 1, -- not in this QFT
}
```

A new recipe shows on the QFT tab as maintaining nothing: it does not run until it is set there.
The rest is set on the QFT tab of the server, per recipe:

| Setting | Meaning |
| --- | --- |
| trigger | `deficit` keeps products stocked: runs when a tracked output is below its target. `surplus` processes raw materials: runs while its inputs are in stock. |
| `target` | Deficit. Amount to keep in the products network; an output without one is not tracked. |
| `priority` | Deficit. Default 1. Higher means more urgent at the same fill level. |
| `weight` | Surplus. How much the output is wanted, for Neptunium. Only the ratio matters; none = 0. |

Before a recipe starts, the stocking bus and hatch slots are set to its inputs. Outputs and
inputs are found in the networks by their internal names, so a label that reads differently in
game does not matter. In Fluid Mode (`fluidMode` in `config/components.lua`) a dust output with
a molten form counts as that fluid (the catalog's `molten`, guessed from the label).

### Adding a catalyst

Put it into the bulk catalyst housing, uncomment its line in `config/catalysts.lua` with the
count and restart the agent; its recipes show on the QFT tab, where their triggers and targets or
weights are set. No new blocks.

## Troubleshooting

- **`not configured: ...`** — some components are unbound, or their block was removed or
  changed. Run `main setup`.
- **A component is bound to the wrong block** — for example, one plasma hatch got the
  other's slot. Run `main setup --all` and connect the blocks one at a time.
- **`config is inconsistent: ...`** — the config files disagree about recipes, or a value is
  wrong; the message says which.


## Not verified in game yet

- Whether an adapter keeps the same component address after leaving and rejoining the world.
  If it does not, setup will have to be redone after every restart.
- Setting the stocking bus and hatch slots from the program for a whole recipe, and the QFT
  taking it up right after (by the source a slot applies at once).
- Whether a stocking bus/hatch starts supplying fast enough after `setWorkAllowed(true)` for
  the QFT to pick the recipe up. The program warns if the machine stays inactive for 2 minutes
  while a recipe should run.
- Both modes, and switching between them. In particular that the stock in the raw materials
  network drops while a surplus recipe runs: the stocking bus pulls from it.
- Whether `amount × catalysts` per cycle matches what the QFT really takes for Platinum Group.

## Development

Type stubs and the general setup are described in the [repository README](../README.md#development).

Local tests (pure logic and the runner with fake components, no game needed):

```sh
cd factoryctl/qft && lua tests/run.lua
```

Notes for AI agents working on the code are in [AGENTS.md](AGENTS.md).
