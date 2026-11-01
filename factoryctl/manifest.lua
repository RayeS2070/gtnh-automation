-- Files installed by tools/gtnh-install.lua: the in-game agents; the server is not for OC.
-- Installed into /home/factoryctl. The crafting agent runs from agent/, the pumping agent from
-- pumps/, the QFT agent from qft/ (both take the shared fc/ modules from ../agent).
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
        'agent/fc/network.lua',
        'agent/fc/probe.lua',
        'agent/fc/scan.lua',
        'agent/fc/trigger.lua',
        'pumps/main.lua',
        'pumps/fp/module.lua',
        'pumps/fp/plan.lua',
        'pumps/fp/pumps.lua',
        'pumps/fp/recipes.lua',
        'pumps/fp/settings.lua',
        'qft/main.lua',
        'qft/qft/bind.lua',
        'qft/qft/config.lua',
        'qft/qft/log.lua',
        'qft/qft/plan.lua',
        'qft/qft/resource.lua',
        'qft/qft/runner.lua',
        'qft/qft/setup.lua',
        'qft/qft/slots.lua',
        'qft/qft/store.lua',
        'qft/tools/qft-dump.lua', -- what the QFT agent sees in game; changes nothing
    },
    config = {
        'agent/config.lua',
        'pumps/config.lua',
        'qft/config/ids.lua',
        'qft/config/components.lua',
        'qft/config/recipes.lua',
    },
    run = 'cd agent && main   (pumping: cd pumps && main; QFT: cd qft && main setup)',
}
