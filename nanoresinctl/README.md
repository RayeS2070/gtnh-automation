# nanoresinctl

An [OpenComputers](https://github.com/GTNewHorizons/OpenComputers) program for
**GregTech: New Horizons 2.8.4** that keeps a set of molten rare-earth metals stocked in an
ME network.

Every 10 minutes the program checks how much of each fluid is stored in the ME network,
picks the one that is needed most and turns on its color in a bundled redstone cable.
Your production line reads that color and decides what to make.

## How it works

1. The program reads the stored amounts with `me_controller.getFluidsInNetwork()`.
2. A fluid counts as **low** when it has less than 80% of its target.
3. Among the low fluids it picks the one with the highest score:

   ```
   score = 1 - (amount / target) ^ priority
   ```

   The emptier the fluid, the higher the score. `priority = 2` makes a fluid more urgent
   than `priority = 1` at the same fill level.
4. It turns on exactly one bundled redstone color on the output side, the color of the
   selected fluid. All other colors are turned off.
5. If no fluid is low, it picks the fluid with the lowest fill level, so the line never stops.
6. It draws a dashboard on the screen. For each fluid it shows a fill bar, the fill level in
   percent, the stored amount and the target. Bar colors:
   - yellow: the fluid being produced now;
   - green: at or above 80% of the target;
   - cyan: below 80% of the target.

If an error occurs, for example when the ME network is unavailable, the program prints it
and tries again at the next check.

## Requirements

- A computer running OpenOS, with a screen and a GPU of tier 2 or higher (tier 1 cannot show colors).
- An **adapter** placed next to the ME controller, so that the `me_controller` component is available.
- A **bundled redstone** output: a Redstone I/O block or a tier 2 redstone card.
  By default the program uses its **bottom** side (`sides.down`).
- Your own setup that switches production of each fluid on and off by its color.

## Installation

Install with the [repository installer](../README.md#installing-in-game) and start the program:

```sh
gtnh-install nanoresinctl
cd /home/nanoresinctl
main
```

Without an internet card, copy `main.lua` into the computer's folder in your world save
(`saves/<world>/opencomputers/<disk address>/home/nanoresinctl/`).

Press **Ctrl+C** to stop the program.

## Configuration

Edit the top of `main.lua`. The installer replaces `main.lua` on every update, so your
changes are lost after `gtnh-install nanoresinctl`.

### Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `kDuration` | `600` | Seconds between checks |
| `kThreshold` | `0.8` | A fluid counts as low below this share of its target |
| `kBundledOutSide` | `sides.down` | Side where the bundled redstone output is |
| `kDebug` | `false` | Print debug logs instead of the dashboard |

### Fluids

Each entry of the `fluids` list describes one fluid. The dashboard shows the fluids in the
order of this list.

```lua
{ label = 'Molten Samarium', target = 1e12, priority = 1, color = colors.white },
```

- `label`: the fluid's display name exactly as the ME network reports it.
- `target`: the amount you want to keep in stock, in mB.
- `priority`: `1` is normal; larger values make the fluid more urgent.
- `color`: the bundled redstone color that turns this fluid's production on.
  Each fluid needs its own color.

Fluids configured by default:

| Fluid | Color | Priority |
| --- | --- | --- |
| Molten Samarium | white | 1 |
| Molten Lanthanum | orange | 2 |
| Molten Cerium | magenta | 1 |
| Molten Praseodymium | light blue | 2 |
| Molten Lutetium | yellow | 1 |
| Molten Holmium | lime | 1 |
| Molten Dysprosium | pink | 1 |
| Molten Gadolinium | gray | 1 |
| Molten Europium | silver | 2 |

## Development

See the [repository README](../README.md#development). Note: the API documentation targets
GTNH 2.9, so on 2.8.4 a few methods might be missing.

## License

[Apache License 2.0](../LICENSE)
