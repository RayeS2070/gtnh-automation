---Components of the QFT setup. Edited by hand, only read by the program.
---Slots made with bind.* get their addresses in setup mode (`main setup`),
---which stores them in /etc/qftctl.bindings, not here.
local bind = require('qft.bind')

-- GTNH 2.9: only a fluid interface or the ME controller answers for fluids.
local kNetwork = { 'fluid_interface', 'me_controller' }

return {
    -- The factoryctl server: settings (targets, weights, mode) come from it and are kept on this
    -- computer; it shows what the QFT does. nil: no server, config/recipes.lua as it is.
    -- OpenComputers refuses private and local addresses by default (internet.filteringRules).
    server = 'http://127.0.0.1:8090',
    -- This setup's name on the server; nil: qft@x,y,z of the controller. Give each QFT its own.
    name = nil,
    controller = bind.manual { type = 'gt_machine', prompt = 'Quantum Force Transformer controller' },
    -- Tier of the Shielding Core Casings (1..4). Focusing works only if it is >= the recipe tier.
    shieldingTier = 1,
    network = {
        source = bind.manual { type = kNetwork, prompt = 'Fluid interface of the RAW MATERIALS network' },
        production = bind.manual { type = kNetwork, prompt = 'Fluid interface of the PRODUCTS network' },
    },
    -- One ME stocking input bus and one hatch serve every recipe: before a recipe starts the
    -- program sets their slots to its inputs (config/recipes.lua). The bus also holds the
    -- programmed circuit for Neptunium. Basic or advanced, both do.
    bus = bind.manual { type = 'me_inputbus', prompt = 'ME stocking input bus of the QFT (items)' },
    hatch = bind.manual { type = 'me_inputhatch', prompt = 'ME stocking input hatch of the QFT (fluids)' },
    -- Plasma: one more ME stocking input hatch, set by the program to the plasma a recipe uses
    -- (none: off). Plasma is stocked from the `source` network; these are its labels there.
    plasma = {
        hatch = bind.manual { type = 'me_inputhatch', prompt = 'ME stocking input hatch for plasma (set by the program)' },
        neptunium = 'Neptunium Plasma',
        fermium = 'Fermium Plasma',
    },
}
