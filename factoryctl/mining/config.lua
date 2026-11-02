-- The mining agent's settings: what this computer has. What the modules mine is the server's (the
-- Mining tab). Edited by hand; `gtnh-install factoryctl` keeps it unless --config is given.
return {
    -- The factoryctl server. OpenComputers refuses private and local addresses by default
    -- (internet.filteringRules in its config on the game server): allow the server's address.
    server = 'http://127.0.0.1:8080',
    -- This computer's setup on the server: give each mining computer its own.
    name = 'mining-1',
    -- How a module's drone gets in, by its controller's coordinates (mining-dump shows them):
    -- the transposer between its plain input bus (the module takes no drone from ME buses) and an
    -- interface of the drone pool, as address prefixes, and their sides of the transposer.
    -- A module without one runs only with the drone it has.
    drones = {
        -- ['169,33,-36'] = { transposer = '83f6', bus = 4, pool = 0, interface = '4ca9' },
    },
    -- The storage buses of the output network, as an address prefix of the me_storagebus and the
    -- side of its cable the bus is on. keep: into processing, the agent writes the wanted ores
    -- in it; void: into the trash with an Inverter Card, the same list (nothing wanted is ever
    -- voided, also when processing is full). nil: no routing.
    routes = {
        keep = nil, -- { bus = '1a40', side = 4 },
        void = nil,
    },
    -- An me_controller or interface of the buffer before processing, as an address prefix: the
    -- kept ores are counted there; nil: the server cannot see processing fall behind.
    buffer = nil,
    -- An interface of the drone pool for counting the drones; nil: the first one there is.
    pool = nil,
    -- 'dbg' shows every step; 'inf' only what happens.
    logLevel = 'inf',
    -- What also goes to the server's log, with the polls ('inf', 'wrn' or 'err').
    forwardLevel = 'inf',
}
