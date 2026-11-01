# GTNH 2.8.4 → 2.9 notes

What we found out about the OpenComputers API while planning the move to GTNH 2.9. The server
runs 2.8.4; the 2.9 results come from a 2.9.rc client. "Verified" means checked in game.

## Files here

Method lists from a 2.9.rc client, `component.doc(address, method)` output. They are not in
the `gtnh/` docs yet.

| File | Component |
| --- | --- |
| [me_inputbus.txt](me_inputbus.txt) | ME Advanced Stocking Input Bus |
| [me_inputhatch.txt](me_inputhatch.txt) | ME Advanced Stocking Input Hatch |

## 2.9.rc

- **Stocking bus and hatch are configurable from OC.** Their drivers `me_inputbus` and
  `me_inputhatch` have `setConfiguration(slot[, detail])`, `getConfiguration(slot)`,
  `getSlotSize()`, `setAutoPull`, `setMinAmount` and `setRefreshTime`. The bus also has
  `setCircuitConfiguration`; the hatch has no circuit methods.
  - Verified: writing a slot and clearing it (`setConfiguration(slot)` with no detail) work.
  - Verified: the configured slots see the full amount in the network, no stack cap.
  - Verified: 16 slots each, on the advanced variants.
  - Not verified: how long a new configuration takes to apply (`getRefreshTime`).
  - The `detail` table (OC `DriverMEInputHatch.scala`, 2.9.0): bus `{name = 'mod:item', damage = N}`
    (or `id`), hatch `{name = 'fluid.name'}`; no detail clears the slot. Slots count from 1.
    `getConfiguration` returns the whole stack. A slot applies at once
    (`setSlotConfigAndUpdate`); `refreshTime` is the auto-pull period.
  - Verified (2.9.0 server): the **basic** stocking bus and hatch have the same drivers and
    16 slots; `isAdvanced()` only tells whether auto-pull is there.
- **OC sees the AE2 storage bus.** `me_storagebus` has
  `setStorageConfiguration(side[, slot][, detail])`. It is no longer needed for qftctl, since
  the stocking bus itself is configurable.
- **Parametrized GT machines** have `getParameters()` / `setParameter(key, val)` in the `main`
  branch stubs of `gtnh/` (`base-gregtech-machine.lua`). The pumping agent (`factoryctl/pumps`)
  depends on them. The Space Elevator pump module's dump (2.9, in game) has both; its parameter
  keys (`batch`, `recipe<N>.planetType`, `.gasType`, `.parallel`) are from GT5-Unofficial's
  `TileEntityModulePump`, not from a dump: `getParameters()` was not printed yet.

## 2.8.4

- **GT stocking bus/hatch** have only `setWorkAllowed` and `setCircuitConfiguration` (the
  latter works in game but is missing from the `gtnh-2.8` stubs). What they show cannot be
  configured from OC, which is why qftctl has one bus per recipe on 2.8.4.
- **OC does not see the storage bus.** No component appears.
- **A storage bus on an ME interface sees the interface's whole network,** even when the
  interface has items configured.
- **A storage bus on an Inventory Relay** (Steve's Factory Manager) placed next to an ME
  interface sees only the interface's own slots. This is effectively a bug. Verified limits:
  64 items per slot (9 slots = 576 items) and 16 buckets of fluid. Too little for real recipes:
  Adhesion Promoter needs 10 buckets per parallel.
- **The `gtnh-2.8` branch of `gtnh/`** has no dumps, only hand-written stubs, and they are
  incomplete. A method missing there may still exist in game; dump it to be sure.
