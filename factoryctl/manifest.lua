-- Files installed by tools/gtnh-install.lua: the in-game agents; the server is not for OC.
-- Installed into /home/factoryctl. The crafting agent runs from agent/, the pumping agent from
-- pumps/ (it takes the shared fc/ modules from ../agent).
return {
    code = {
        'agent/main.lua',
        'agent/fc/agent.lua',
        'agent/fc/http.lua',
        'agent/fc/jobs.lua',
        'agent/fc/json.lua',
        'agent/fc/log.lua',
        'agent/fc/logbuf.lua',
        'agent/fc/me.lua',
        'agent/fc/probe.lua',
        'agent/fc/scan.lua',
        'agent/fc/trigger.lua',
        'pumps/main.lua',
        'pumps/fp/module.lua',
        'pumps/fp/plan.lua',
        'pumps/fp/pumps.lua',
        'pumps/fp/recipes.lua',
        'pumps/fp/settings.lua',
    },
    config = {
        'agent/config.lua',
        'pumps/config.lua',
    },
    run = 'cd agent && main   (pumping: cd pumps && main)',
}
