-- Files installed by tools/gtnh-install.lua. A new module must be added here, or it will not be installed.
return {
    code = {
        'main.lua',
        'qft/bind.lua',
        'qft/config.lua',
        'qft/log.lua',
        'qft/plan.lua',
        'qft/resource.lua',
        'qft/runner.lua',
        'qft/setup.lua',
        'qft/store.lua',
    },
    config = {
        'config/ids.lua',
        'config/components.lua',
        'config/recipes.lua',
    },
    run = 'main setup',
}
