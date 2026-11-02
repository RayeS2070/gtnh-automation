-- The pumping agent's settings. The server's settings (targets, batch, threshold) replace
-- `defaults` once it gave some: they are saved next to this file and kept across restarts, so the
-- modules keep the levels without the server. Edited by hand; `gtnh-install factoryctl` keeps it
-- unless --config is given.
return {
    -- The factoryctl server. OpenComputers refuses private and local addresses by default
    -- (internet.filteringRules in its config on the game server): allow the server's address.
    server = 'http://127.0.0.1:8080',
    -- The pumping network's fluid_interface or me_controller, as a prefix of its address; nil = the
    -- only one. Its stock is what the targets are about.
    me = nil,
    -- 'dbg' shows every step; 'inf' only what happens.
    logLevel = 'inf',
    -- What also goes to the server's log, with the polls ('inf', 'wrn' or 'err').
    forwardLevel = 'inf',
    -- Until the server gives its own. fluid: the internal name (fp/recipes.lua); target: mB to
    -- keep; priority: a greater one is pumped sooner at the same fill (1 - fill^priority).
    defaults = {
        batch = 30,      -- seconds of a cycle, 1..128
        threshold = 0.95, -- a fluid below threshold * target is pumped
        targets = {
            { fluid = 'chlorobenzene', target = 10000000000, priority = 1 },
            { fluid = 'endergoo', target = 10000000000, priority = 1 },
            { fluid = 'lava', target = 10000000000, priority = 1 },
            { fluid = 'sulfuricacid', target = 10000000000, priority = 1 },
            { fluid = 'molten.iron', target = 10000000000, priority = 2 },
            { fluid = 'oil', target = 10000000000, priority = 1 },
            { fluid = 'molten.lead', target = 10000000000, priority = 1 },
            { fluid = 'carbondioxide', target = 1000000000, priority = 1 },
            { fluid = 'carbonmonoxide', target = 10000000000, priority = 1 },
            { fluid = 'helium-3', target = 10000000000, priority = 1 },
            { fluid = 'saltwater', target = 10000000000, priority = 1 },
            { fluid = 'helium', target = 10000000000, priority = 3 },
            { fluid = 'neon', target = 1000000000, priority = 1 },
            { fluid = 'argon', target = 1000000000, priority = 1 },
            { fluid = 'krypton', target = 1000000000, priority = 1 },
            { fluid = 'methane', target = 1000000000, priority = 1 },
            { fluid = 'deuterium', target = 10000000000, priority = 1 },
            { fluid = 'tritium', target = 10000000000, priority = 1 },
            { fluid = 'ammonia', target = 10000000000, priority = 2 },
            { fluid = 'xenon', target = 10000000000, priority = 2 },
            { fluid = 'ethylene', target = 10000000000, priority = 1 },
            { fluid = 'hydrofluoricacid_gt5u', target = 10000000000, priority = 1 },
            { fluid = 'fluorine', target = 10000000000, priority = 1 },
            { fluid = 'nitrogen', target = 10000000000, priority = 3 },
            { fluid = 'oxygen', target = 10000000000, priority = 3 },
            { fluid = 'hydrogen', target = 10000000000, priority = 3 },
            { fluid = 'molten.copper', target = 10000000000, priority = 2 },
            { fluid = 'unknowwater', target = 10000000000, priority = 1 },
            { fluid = 'ic2distilledwater', target = 10000000000, priority = 1 },
            { fluid = 'radon', target = 1000000000, priority = 1 },
            { fluid = 'molten.tin', target = 10000000000, priority = 1 },
        },
    },
}
