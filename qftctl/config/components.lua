---Components of the QFT setup. Edited by hand, only read by the program.
---Slots made with bind.* get their addresses in setup mode (`main setup`),
---which stores them in /etc/qftctl.bindings, not here.
local bind = require('qft.bind')

-- GTNH 2.9: only a fluid interface or the ME controller answers for fluids.
local kNetwork = { 'fluid_interface', 'me_controller' }

return {
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
    -- Plasma has hatches of its own, set by hand to it. It is stocked from the `source` network;
    -- `fluid` is the label used to check the stock.
    plasma = {
        neptunium = {
            hatch = bind.manual { type = 'me_inputhatch', prompt = 'ME stocking input hatch with Neptunium Plasma' },
            fluid = 'Neptunium Plasma',
        },
        fermium = {
            hatch = bind.manual { type = 'me_inputhatch', prompt = 'ME stocking input hatch with Fermium Plasma' },
            fluid = 'Fermium Plasma',
        },
    },
}
