---Components of the QFT setup. Edited by hand, only read by the program.
---Slots made with bind.* get their addresses in setup mode (`main setup`),
---which stores them in /etc/qftctl.bindings, not here.
local bind = require('qft.bind')
local RecipeId = require('config.ids')

return {
    controller = bind.manual { type = 'gt_machine', prompt = 'Quantum Force Transformer controller' },
    -- Tier of the Shielding Core Casings (1..4). Focusing works only if it is >= the recipe tier.
    shieldingTier = 1,
    network = {
        source = bind.manual { type = 'me_interface', prompt = 'ME interface of the RAW MATERIALS network' },
        production = bind.manual { type = 'me_interface', prompt = 'ME interface of the PRODUCTS network' },
    },
    -- Plasma is stocked from the `source` network. `fluid` is the label used to check the stock.
    plasma = {
        neptunium = {
            hatch = bind.manual { type = 'gt_machine', prompt = 'Stocking input hatch with Neptunium Plasma' },
            fluid = 'Neptunium Plasma',
        },
        fermium = {
            hatch = bind.manual { type = 'gt_machine', prompt = 'Stocking input hatch with Fermium Plasma' },
            fluid = 'Fermium Plasma',
        },
    },
    -- Per recipe, enabled only while it runs:
    --   bus   required: stocking input bus; it also holds the programmed circuit for Neptunium.
    --   hatch optional: stocking input hatch, only for recipes with fluid inputs.
    recipes = {
        [RecipeId.AdhesionPromoter] = {
            bus = bind.manual { type = 'gt_machine', prompt = 'Stocking input bus for Adhesion Promoter' },
            hatch = bind.manual { type = 'gt_machine', prompt = 'Stocking input hatch for Adhesion Promoter' },
        },
        [RecipeId.RubberPolymer] = {
            bus = bind.manual { type = 'gt_machine', prompt = 'Stocking input bus for Rubber Polymer' },
            hatch = bind.manual { type = 'gt_machine', prompt = 'Stocking input hatch for Rubber Polymer' },
        },
        [RecipeId.PlatinumGroup] = {
            bus = bind.manual { type = 'gt_machine', prompt = 'Stocking input bus for Platinum Group' },
        },
    },
}
