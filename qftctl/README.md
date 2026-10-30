# qftctl

An OpenComputers program that controls a **Quantum Force Transformer** (QFT) in the
GregTech: New Horizons modpack.

> **Status:** setup, both modes (maintaining products and processing raw materials) and plasma
> focusing are written and tested locally. See [Not verified in game yet](#not-verified-in-game-yet).

## Why setup is needed

The AE2 API gives a program no way to tell two ME networks apart: an ME interface of the raw
materials network and one of the products network look exactly the same. The program still has
to know which is which, and which stocking hatch holds which plasma. So you tell it once, in
**setup mode**, and it remembers the component addresses.

## Requirements

- An OpenComputers computer running OpenOS, with the Lua 5.3 architecture (the default).
- An Adapter next to every block the program talks to:
  - one ME interface in the **raw materials** network and one in the **products** network;
  - the Quantum Force Transformer controller;
  - the stocking input hatches with Neptunium Plasma and Fermium Plasma;
  - for each recipe, its stocking input bus and, if the recipe has fluid inputs, its stocking
    input hatch.

## Installation

Install with the [repository installer](../README.md#installing-in-game):

```
gtnh-install qftctl
```

This installs into `/home/qftctl`, or updates an existing installation. `gtnh-install qftctl --config`
also overwrites the files in `config/` with the ones from the repository.

The program code is always replaced. The files in `config/` are downloaded only when they are
missing: they hold your setup, so an update does not touch them unless you pass `--config`.
If a kept file differs from the repository version, the installer says so
(`kept config/components.lua (DIFFERS from the repository, ...)`) — check what changed
upstream before replacing it, your own edits would be lost.
Saved component addresses (`/etc/qftctl.bindings`) are never touched by the installer.

Without an Internet Card, copy `main.lua`, `config/` and `qft/` into one directory by hand.

Run the program **from its directory** (`cd /home/qftctl`): modules are loaded relative to
the current directory.

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

Every 10 seconds the program reads the products and raw materials networks and decides which
recipe should run. Each recipe has a `trigger`, and the program works in one of two modes:

**Maintaining products** (`trigger = 'deficit'`) comes first:

- A recipe **starts** when one of its tracked outputs fell below 80% of its `target`.
- A running recipe **keeps going** until every tracked output reaches its `target`. Without
  this gap the QFT would switch on and off around the target on every poll.
- If several recipes are low, the most urgent one wins. Urgency of an output is
  `1 - (amount / target) ^ priority`.
- If the recipe has `inputs`, it needs raw materials for one cycle to start and to keep going.

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
then does it switch the stocking buses/hatches and start again. On start the program first
stops the machine and switches every input off. On Ctrl+C it stops the controller and leaves
the inputs as they are.

**Fermium Plasma.** A recipe runs with Fermium when both hold:

- the Shielding Core tier is not below the recipe tier (otherwise plasma is spent for nothing);
- the source network holds enough plasma. The use rate is `4 × shieldingTier × √catalysts`
  L/s (per the GTNH wiki). To start, the stock must cover 600 s. While running it must cover
  the current cycle (read from the controller) plus 60 s.

When the stock gets too low, the recipe finishes its cycle and restarts without Fermium. The
QFT never runs dry mid-cycle, because that would shut it down until a manual restart.
**Neptunium Plasma.** Neptunium moves probability between outputs, it adds none, so it
pays only when the outputs are needed unequally. For every option (no focus, focus on each
output) the program computes `Σ weight × chance`, with the chances ported from the QFT source.
The weight of an output is its urgency when maintaining products, and its `weight` from the
config when processing raw materials.
Neptunium goes on when the best focus beats no focus by 10% and the stock is enough (same
rule as Fermium). It stays on while the focus still beats no focus by 5%. The focus moves
to another output when that output beats it by 10%.

- On this server the Neptunium hatch burns plasma even without a circuit. So the circuit is
  set on the recipe's stocking bus **before** the hatch is enabled, and reset to `-1` only
  after every hatch is off.
- Moving the focus only changes the circuit, without stopping the machine. Switching
  Neptunium on or off goes through a stop after the cycle, like Fermium.
- `circuit` of the outputs must cover 1..N: the chances depend on the number of outputs, so
  every output of the recipe has to be listed (outputs without `target` are fine).

Saved addresses live in `/etc/qftctl.bindings`. Deleting that file is the same as
`main setup --all`.

## Setup step by step

1. **Configuration check.** Before anything else the program checks that `config/components.lua`
   and `config/recipes.lua` describe the same recipes (see [Configuration](#configuration)).
   If they do not, it prints the mismatch and stops.
2. **Walk over the components.** Setup goes over every component in `config/components.lua`
   in alphabetical order of its path (`network.production`, `network.source`,
   `plasma.fermium.hatch`, ...). A component that is already bound and still present prints `ok` and
   is skipped; with `--all` every saved address is forgotten and everything is bound again.
3. **Binding a component** — the program prints what it needs, for example:
   ```
   [network.source] ME interface of the RAW MATERIALS network
     Connect the me_interface now, or connect it and press Enter. [s] skip.
   ```
   Then **connect that block** to the computer: place the adapter next to it, or break and
   place the adapter back if it is already there. Either:
   - the program notices the new component by itself and binds it right away, or
   - press **Enter** after connecting it: the program compares the list of components with the
     one from before the prompt and takes the single new one.

   Other keys while waiting:
   - **`s`** — skip this component, it stays unbound;
   - **Ctrl+C** — stop setup. Everything bound so far is kept.
4. **Saving.** Each address is written to `/etc/qftctl.bindings` as soon as it is bound, so
   stopping or crashing halfway loses nothing.
5. **Result.** At the end setup lists the components left unbound, if any. Run `main setup`
   again to finish them.

Safety checks during setup:

- one block cannot be bound to two components — for example, the same ME interface as both
  raw materials and products. Setup says which component already holds it and keeps waiting;
- a component of the wrong type is ignored — an ME interface will not be bound where a stocking
  hatch is expected;
- if several new components of the right type show up at once, setup does not guess and asks
  you to connect them one at a time.

## Configuration

The configuration is split into three files. You edit them by hand; the program only reads them.

| File | What it holds |
| --- | --- |
| `config/ids.lua` | Recipe identifiers. They link the two files below. |
| `config/components.lua` | Which components the setup needs and how to find them. |
| `config/recipes.lua` | Recipe data: trigger, inputs, outputs, targets and weights. |

### Components

```lua
network = {
    source = bind.manual{ type = 'me_interface', prompt = 'ME interface of the RAW MATERIALS network' },
},
```

- `bind.manual{ type, prompt }` — a component bound in setup mode; `prompt` is what the
  player sees while setup waits for it.
- `type` is the OpenComputers component type. GregTech machines and hatches are `gt_machine`,
  ME interfaces are `me_interface`.
- `bind.manual` accepts only `type` and `prompt`. Data about a component goes next to it, not
  into it: `plasma.neptunium = { hatch = bind.manual{...}, fluid = 'Neptunium Plasma' }`.
- `shieldingTier` — the tier of the Shielding Core Casings (1..4).
- `recipes[id].bus` (required) — the recipe's stocking input bus; it also holds the programmed
  circuit for Neptunium focusing. `recipes[id].hatch` (optional) — its stocking input hatch.

### Recipes

A recipe that maintains products:

```lua
[RecipeId.RubberPolymer] = {
    name = 'Rubber Polymer',
    trigger = 'deficit',
    tier = 1,      -- recipe tier (pulse manipulator tier that unlocks it)
    catalysts = 8, -- catalysts in the bulk catalyst housing = parallels; 0 = never run it
    outputs = {
        { fluid = 'Molten Silicone Rubber', circuit = 1, target = 10e9, priority = 1 }, -- 10e9 mB
        { fluid = 'Molten Rubber',          circuit = 4 },                -- no target: not tracked
    },
},
```

A recipe that processes raw materials:

```lua
[RecipeId.PlatinumGroup] = {
    name = 'Platinum Group',
    trigger = 'surplus',
    tier = 1,
    catalysts = 1,
    inputs = {
        { item = 'Platinum Metallic Powder Dust', amount = 32 }, -- per parallel per cycle
    },
    outputs = {
        { fluid = 'Molten Platinum',  circuit = 1, weight = 2 },
        { fluid = 'Molten Palladium', circuit = 2, weight = 1 },
        { fluid = 'Molten Iridium',   circuit = 3 },             -- no weight: not wanted
        -- ... every output, see `circuit` below
    },
},
```

An output is `item = <label>` or `fluid = <label>`, as the game shows it. Items are counted in
pieces, fluids in mB, and `target` uses the same unit. If a label is ambiguous (the program
reports which internal ids it matches), add `name` and, for items, `damage`. Items that differ
only by NBT are reported as ambiguous. AE2FC fluid drops are ignored. The products network
is asked for items only when some tracked output is an item.

`circuit` is the position of the output in NEI: the programmed circuit number that focuses it
with Neptunium.

| Field | Trigger | Meaning |
| --- | --- | --- |
| `target` | deficit | Amount to keep in the products network. An output without `target` is not tracked; at least one output needs it. |
| `priority` | deficit | Default 1. Shapes how urgent a tracked output is at the same fill level: higher means more urgent. |
| `weight` | surplus | How much the output is wanted, for Neptunium. Only the ratio between outputs matters; absent means 0. Equal weights mean Neptunium never pays. |

A field of the other trigger is an error: it usually means the trigger is wrong.

`inputs` is a list of raw materials in the source network, described like outputs plus
`amount`: how much one parallel takes per cycle, in pieces or mB as in NEI. A surplus recipe
needs `inputs`. For a deficit recipe they are optional: without them it is treated as
supplied without limit.

### Adding a recipe

A recipe goes into three places:

1. `config/ids.lua` — add an id: `MyRecipe = 'my_recipe'`.
2. `config/components.lua` — under `recipes`, add `[RecipeId.MyRecipe] = { bus = bind.manual{...} }`
   (plus `hatch = bind.manual{...}` if the recipe has fluid inputs).
3. `config/recipes.lua` — add `[RecipeId.MyRecipe] = { name, trigger, tier, catalysts, inputs, outputs }`.

Then run `main setup` to bind the new bus/hatch. If you forget one of the files, the program
tells you which recipe is missing where; a mistyped `RecipeId.X` stops it with
`unknown recipe id: X`.

## Troubleshooting

- **`not configured: ...`** — some components are unbound, or their block was removed or
  changed. Run `main setup`.
- **A component is bound to the wrong block** — for example, one plasma hatch got the
  other's slot. Run `main setup --all` and connect the blocks one at a time.
- **`config is inconsistent: ...`** — the config files disagree about recipes; the message says
  which recipe is missing where.

## Not verified in game yet

- Whether an adapter keeps the same component address after leaving and rejoining the world.
  If it does not, setup will have to be redone after every restart.
- Whether `setWorkAllowed(false)` really stops a stocking bus (for the plasma stocking hatch it
  does).
- Whether `getWorkMaxProgress()` of the QFT controller returns the cycle length in ticks.
  The plasma reserve while running depends on it.
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
cd qftctl && lua tests/run.lua
```

Notes for AI agents working on the code are in [AGENTS.md](AGENTS.md).
