---Components of the QFT setup. Edited by hand, only read by the program.
---Slots made with bind.* get their addresses in setup mode (`main setup`),
---which stores them in /etc/qftctl.bindings, not here. bind.auto slots are found by setup
---itself when the computer sees as many components of their type as there are such slots.
local bind = require('qft.bind')

-- GTNH 2.9: only a fluid interface or the ME controller answers for fluids.
local kNetwork = { 'fluid_interface', 'me_controller' }

return {
    -- The factoryctl server: settings (targets, weights, mode) come from it and are kept on this
    -- computer; it shows what the QFT does. nil: no server, config/recipes.lua as it is.
    -- OpenComputers refuses private and local addresses by default (internet.filteringRules).
    server = 'http://127.0.0.1:8080',
    -- This setup's name on the server; nil: qft@x,y,z of the controller. Give each QFT its own.
    name = nil,
    controller = bind.manual { type = 'gt_machine', prompt = 'Quantum Force Transformer controller' },
    -- Tier of the Shielding Core Casings (1..4). Focusing works only if it is >= the recipe tier.
    shieldingTier = 1,
    -- Fluid Mode of the QFT (screwdriver): dust outputs come as molten metal (qft/catalog.lua says
    -- which). Set it as the machine is set.
    fluidMode = true,
    network = {
        source = bind.manual { type = kNetwork, prompt = 'Fluid interface of the RAW MATERIALS network' },
        production = bind.manual { type = kNetwork, prompt = 'Fluid interface of the PRODUCTS network' },
    },
    -- One ME stocking input bus and one hatch serve every recipe: before a recipe starts the
    -- program sets their slots to its inputs (config/recipes.lua). The bus also holds the
    -- programmed circuit for Neptunium. Basic or advanced, both do.
    -- The bus and both hatches stock from the `source` network, so either hatch can be either:
    -- setup takes the only bus and the only two hatches the computer sees.
    bus = bind.auto { type = 'me_inputbus', prompt = 'ME stocking input bus of the QFT (items)' },
    hatch = bind.auto { type = 'me_inputhatch', prompt = 'ME stocking input hatch of the QFT (fluids)' },
    -- Plasma: one more ME stocking input hatch, set by the program to the plasma a recipe uses
    -- (none: off). Plasma is stocked from the `source` network; these are its labels there.
    plasma = {
        hatch = bind.auto { type = 'me_inputhatch', prompt = 'ME stocking input hatch for plasma (set by the program)' },
        neptunium = 'Neptunium Plasma',
        fermium = 'Fermium Plasma',
    },
}
