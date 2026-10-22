---Recipe data for craft calculations. Edited by hand, only read by the program.
local RecipeId = require('config.ids')

-- TODO: item support for inputs (trigger = 'surplus' recipes need it). Outputs already take
-- items: see qft/resource.lua. Decision notes:
-- https://claude.ai/code/artifact/13942bc7-ff65-42f2-bd88-29b387c70e63

---An output is either `item = <label>` or `fluid = <label>`, as the game shows it.
---If the label is ambiguous (the program says so), add `name` and, for items, `damage`.
---Items are counted in pieces, fluids in mB: `target` uses the same unit.
---@class qft.RecipeOutput
---@field item string? # Label of an item output.
---@field fluid string? # Label of a fluid output.
---@field name string? # Internal id, only to resolve an ambiguous label.
---@field damage integer? # Items only: metadata, only to resolve an ambiguous label.
---@field resource qft.Resource # Built from the fields above when the config is loaded.
---@field circuit integer # Position of the output in NEI = programmed circuit for Neptunium focusing.
---@field target number? # Stop producing once this amount is reached; nil means not tracked.
---@field priority number? # Urgency exponent for tracked outputs: 1 - (amount/target)^priority.
---                        # Higher = more urgent at the same fill level. Default 1.
---@field distribution number? # TODO: becomes `weight` for trigger = 'surplus' recipes, see
---                            # https://claude.ai/code/artifact/e97adf35-7231-4966-abc1-418470c8eb23

---@class qft.Recipe
---@field name string # Display name.
---@field tier integer # Recipe tier (1..4): the pulse manipulator tier that unlocks it.
---@field catalysts integer # Catalysts in the bulk catalyst housing = parallels.
---@field inputs table? # Tracked inputs; nil means supplied without limit.
---@field outputs qft.RecipeOutput[]

---@type table<qft.RecipeId, qft.Recipe>
return {
    [RecipeId.AdhesionPromoter] = {
        name = 'Adhesion Promoter',
        tier = 1, -- wiki,
        catalysts = 4,
        outputs = {
            { item = 'Hyper-Stable Self-Healing Adhesive', circuit = 1, target = 4096, priority = 3 },
            { fluid = 'Ethyl Cyanoacrylate (Super Glue)',  circuit = 2, priority = 1 },
            { fluid = 'Advanced Glue',                     circuit = 3, priority = 1 },
            { fluid = 'Molten Indalloy 140',               circuit = 4, target = 1e8, priority = 2 },
            { fluid = 'Molten Soldering Alloy',            circuit = 5, target = 1e7, priority = 1 }
        }
    },
    [RecipeId.RubberPolymer] = {
        name = 'Rubber Polymer',
        tier = 1, -- wiki
        catalysts = 1,
        -- No `inputs`: this recipe is supplied without limit, nothing to track.
        outputs = {
            { fluid = 'Molten Silicone Rubber',          circuit = 1, target = 10e8, priority = 2 },
            { fluid = 'Molten Styrene-Butadiene Rubber', circuit = 2, target = 10e6, priority = 1 },
            { fluid = 'Molten Polyphenylene Sulfide',    circuit = 3, target = 10e8, priority = 2 },
            { fluid = 'Molten Rubber',                   circuit = 4, target = 10e6, priority = 1 },
        },
    },
    [RecipeId.PlatinumGroup] = {
        name = 'Platinum Group',
        tier = 1, -- wiki
        catalysts = 1,
        inputs = {
            item = 'Platinum Metallic Powder Dust',
            amount = 32
        },
        outputs = {
            { fluid = 'Molten Platinum',  circuit = 1 },
            { fluid = 'Molten Palladium', circuit = 2 },
            { fluid = 'Molten Iridium',   circuit = 3 },
            { fluid = 'Molten Osmium',    circuit = 4 },
            { fluid = 'Molten Rhodium',   circuit = 5 },
            { fluid = 'Molten Ruthenium', circuit = 6 }
        }
    }
}
