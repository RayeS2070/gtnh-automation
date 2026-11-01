-- The agent's settings. Everything else (poll interval, scan parameters, orders) comes from the
-- server. Edited by hand; `gtnh-install factoryctl` keeps it unless --config is given.
return {
    -- The factoryctl server. OpenComputers refuses private and local addresses by default
    -- (internet.filteringRules in its config on the game server): allow the server's address
    -- there, or use an address it does not filter.
    server = 'http://127.0.0.1:8080',
    -- The ME component to use, as a prefix of its address; nil = the only one. Looked for in this
    -- order: fluid_interface, me_controller, me_interface (2.9 fluids need one of the first two).
    -- Several of one network are fine, any of them works; give one anyway.
    interface = nil,
    -- 'dbg' shows every poll; 'inf' only what happens.
    logLevel = 'inf',
    -- What also goes to the server's log, with the polls ('inf', 'wrn' or 'err').
    forwardLevel = 'inf',
}
