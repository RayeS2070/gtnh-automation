-- Files installed by tools/gtnh-install.lua: only the in-game agent; the server is not for OC.
-- Installed into /home/factoryctl; run it from the agent directory.
return {
    code = {
        'agent/main.lua',
        'agent/fc/agent.lua',
        'agent/fc/http.lua',
        'agent/fc/jobs.lua',
        'agent/fc/json.lua',
        'agent/fc/log.lua',
        'agent/fc/me.lua',
        'agent/fc/scan.lua',
    },
    config = {
        'agent/config.lua',
    },
    run = 'cd agent && main',
}
