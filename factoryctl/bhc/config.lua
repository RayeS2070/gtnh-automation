-- The black hole agent's settings: what this computer has. Edited by hand; `gtnh-install
-- factoryctl` keeps it unless --config is given. bhc-dump shows the addresses and the sides.
local sides = require 'sides'

return {
    -- Address prefixes. nil: the only component of its kind (a BHC controller is found by its
    -- name among the gt_machine adapters).
    controller = nil,
    redstone = nil,
    transposer = nil,
    -- The spacetime hatch: an ME stocking input hatch of the supplies subnet in a spacetime place
    -- of the structure, auto-pull off, all slots empty (the agent sets and clears `hatchSlot`).
    -- The stocking hatch of the ingredients is an me_inputhatch too: give this one's prefix.
    hatch = nil,
    hatchSlot = 1,
    -- The two subnets look alike to OC: both must be given (fluid_interface, me_controller or
    -- me_interface of each).
    ingredients = 'e721', -- what the recipes eat, behind the stocking bus/hatch of the BHC
    supplies = '99eb',    -- spacetime, seeds and collapsers
    -- Sides (sides.* numbers, 0 bottom, 1 top, 2 north, 3 south, 4 west, 5 east).
    sides = {
        pulse = sides.bottom,   -- of the Redstone I/O: the Utility Hatch
        supplies = sides.east,  -- of the transposer: the supplies interface, seeds and collapsers
        bus = sides.west,       -- of the transposer: a plain input bus of the BHC, for catalysts
        heartbeat = sides.top,  -- of the Redstone I/O: a square wave while the agent runs (nil: off)
    },
    -- s of the heartbeat's period: 255 for half of it, 0 for the other half. A watchdog should
    -- allow a few periods: a slow step or a disk write may delay a beat.
    heartbeatEvery = 1,
    -- The spacetime fluid's internal name.
    spacetime = 'molten.spacetime',
    -- What the agent makes the supplies interface stock at start (slots from 1). A slot holding
    -- something else is left alone. nil: stocked by hand.
    stock = {
        seedSlot = 1, seeds = 2,
        collapserSlot = 2, collapsers = 2,
    },
    -- Stability at which holding starts: below 20 the parallels are x4 already, so lower is
    -- cheaper; the gap to 0 is the margin for a missed pulse.
    holdAt = 5,
    -- Work stops if the stability gets this low anyway.
    floor = 2,
    -- Holds per session while there is new work; a running recipe is held to its end anyway.
    -- 120 holds cost 450 L; every 30 more double the cost per second.
    maxHolds = 120,
    -- Spacetime the supplies must have to open a hole, L.
    minSpacetime = 10000,
    -- s between looks at the ingredients subnet.
    workEvery = 5,
    -- nil: /home/factoryctl/bhc/state.dat
    statePath = nil,
    -- 'dbg' shows every pulse; 'inf' only what happens.
    logLevel = 'inf',
}
