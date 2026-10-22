---Recipe data for craft calculations. Edited by hand, only read by the program.
local RecipeId = require('config.ids')

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
---@field target number? # Deficit only: stop producing once this amount is reached; nil = not tracked.
---@field priority number? # Deficit only: urgency exponent, 1 - (amount/target)^priority.
---                        # Higher = more urgent at the same fill level. Default 1.
---@field weight number? # Surplus only: how much this output is wanted, for Neptunium focusing.
---                      # Only the ratio between outputs matters; absent = 0, not wanted.

---An input is described like an output, plus how much of it one cycle takes.
---@class qft.RecipeInput
---@field item string?
---@field fluid string?
---@field name string?
---@field damage integer?
---@field resource qft.Resource # Built when the config is loaded.
---@field amount number # Per parallel per cycle: pieces or mB, as in NEI.

---Why a recipe runs:
---  'deficit' maintains products: runs when a tracked output is below its target.
---  'surplus' processes raw materials: runs when no product is short and its inputs are in stock.
---@alias qft.Trigger 'deficit' | 'surplus'

---@class qft.Recipe
---@field name string # Display name.
---@field trigger qft.Trigger
---@field tier integer # Recipe tier (1..4): the pulse manipulator tier that unlocks it.
---@field catalysts integer # Catalysts in the bulk catalyst housing = parallels.
---@field inputs qft.RecipeInput[]? # Raw materials in the source network; required for surplus.
---                                 # Deficit: optional, nil means supplied without limit.
---@field outputs qft.RecipeOutput[]

---@type table<qft.RecipeId, qft.Recipe>
return {
    [RecipeId.AdhesionPromoter] = {
        name = 'Adhesion Promoter',
        trigger = 'deficit',
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
        trigger = 'deficit',
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
        trigger = 'surplus',
        tier = 1, -- wiki
        catalysts = 1,
        inputs = {
            { item = 'Platinum Metallic Powder Dust', amount = 32 },
        },
        -- TODO: weights are placeholders. Equal weights mean Neptunium never pays.
        outputs = {
            { fluid = 'Molten Platinum',  circuit = 1, weight = 100 },
            { fluid = 'Molten Palladium', circuit = 2, weight = 100 },
            { fluid = 'Molten Iridium',   circuit = 3, weight = 200 },
            { fluid = 'Molten Osmium',    circuit = 4, weight = 200 },
            { fluid = 'Molten Rhodium',   circuit = 5, weight = 50 },
            { fluid = 'Molten Ruthenium', circuit = 6, weight = 50 }
        }
    }
}
