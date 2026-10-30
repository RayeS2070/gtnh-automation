-- The agent's settings. Everything else (poll interval, scan parameters, orders) comes from the
-- server. Edited by hand; `gtnh-install factoryctl` keeps it unless --config is given.
return {
    -- The factoryctl server. OpenComputers refuses private and local addresses by default
    -- (internet.filteringRules in its config on the game server): allow the server's address
    -- there, or use an address it does not filter.
    server = 'http://127.0.0.1:8080',
    -- The me_interface (or me_controller) to use, as a prefix of its address; nil = the only one.
    -- Several interfaces of one network are fine, any of them works; give one anyway.
    interface = nil,
    -- 'dbg' shows every poll; 'inf' only what happens.
    logLevel = 'inf',
}
