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

---An input is described like an output, plus how much of it one cycle takes. Every input of the
---recipe is listed: the stocking bus (items) and hatch (fluids) are set from them before the
---recipe starts, in this order. The slot needs the internal name: the program finds it in the
---raw materials network by the label; `name` (and `damage`) here spare that lookup and work when
---the network has none of it.
---@class qft.RecipeInput
---@field item string?
---@field fluid string?
---@field name string?
---@field damage integer?
---@field resource qft.Resource # Built when the config is loaded.
---@field amount number? # Per parallel per cycle: pieces or mB, as in NEI. Counted: the recipe
---                      # waits for stock for a cycle. Absent: supplied without limit, not counted
---                      # (surplus recipes need it on every input).

---Why a recipe runs:
---  'deficit' maintains products: runs when a tracked output is below its target.
---  'surplus' processes raw materials: runs when no product is short and its inputs are in stock.
---@alias qft.Trigger 'deficit' | 'surplus'

---@class qft.Recipe
---@field name string # Display name.
---@field trigger qft.Trigger
---@field tier integer # Recipe tier (1..4): the pulse manipulator tier that unlocks it.
---@field catalysts integer # Catalysts in the bulk catalyst housing = parallels; 0 = recipe disabled.
---@field inputs qft.RecipeInput[] # Every input, from the source network: 16 items, 16 fluids at most.
---@field outputs qft.RecipeOutput[]

---@type table<qft.RecipeId, qft.Recipe>
return {
    [RecipeId.AdhesionPromoter] = {
        name = 'Adhesion Promoter',
        trigger = 'deficit',
        tier = 1, -- wiki,
        catalysts = 4,
        inputs = {
            -- TODO(user): every input of the recipe, e.g. { fluid = 'Oxygen' } or
            -- { item = 'Some Dust', amount = 32 } (amount: counted, see qft.RecipeInput).
        },
        outputs = {
            { item = 'Hyper-Stable Self-Healing Adhesive', circuit = 1, target = 4096, priority = 3 },
            { fluid = 'Ethyl Cyanoacrylate (Super Glue)',  circuit = 2, priority = 1 },
            { fluid = 'Advanced Glue',                     circuit = 3, priority = 1 },
            { fluid = 'Molten Indalloy 140',               circuit = 4, target = 1e8,  priority = 2 },
            { fluid = 'Molten Soldering Alloy',            circuit = 5, target = 1e7,  priority = 1 }
        }
    },
    [RecipeId.RubberPolymer] = {
        name = 'Rubber Polymer',
        trigger = 'deficit',
        tier = 1, -- wiki
        catalysts = 1,
        -- Supplied without limit: inputs without an amount, set in the bus and hatch, not counted.
        inputs = {
            -- TODO(user): every input of the recipe, e.g. { fluid = 'Oxygen' }.
        },
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
        outputs = {
            { fluid = 'Molten Platinum',  circuit = 1, weight = 100 },
            { fluid = 'Molten Palladium', circuit = 2, weight = 100 },
            { fluid = 'Molten Iridium',   circuit = 3, weight = 200 },
            { fluid = 'Molten Osmium',    circuit = 4, weight = 200 },
            { fluid = 'Molten Rhodium',   circuit = 5, weight = 50 },
            { fluid = 'Molten Ruthenium', circuit = 6, weight = 50 }
        }
    },
    [RecipeId.InertNaquadah] = {
        name = 'Simple Naquadah',
        trigger = 'surplus',
        tier = 2, -- wiki
        catalysts = 16,
        inputs = {
            { item = 'Naquadah Oxide Mixture Dust', amount = 32 },
            { item = 'Sodium Dust',                 amount = 64 },
            { item = 'Carbon Dust',                 amount = 1 }
        },
        outputs = {
            { item = 'Inert Naquadah Dust', circuit = 1, weight = 600 },
            { fluid = 'Molten Titanium',    circuit = 2, weight = 100 },
            { fluid = 'Molten Adamantium',  circuit = 3, weight = 100 },
            { fluid = 'Molten Gallium',     circuit = 4, weight = 200 },
        }
    },
    [RecipeId.InertEnrichedNaquadah] = {
        name = 'Inert Enriched Naquadah',
        trigger = 'surplus',
        tier = 2, -- wiki
        catalysts = 4,
        inputs = {
            { item = 'Enriched-Naquadah Oxide Mixture Dust', amount = 32 },
            { item = 'Zinc Dust',                            amount = 64 },
            { item = 'Carbon Dust',                          amount = 1 }
        },
        outputs = {
            { item = 'Inert Enriched Naquadah Dust', circuit = 1, weight = 900 },
            { fluid = 'Molten Trinium',              circuit = 2, weight = 100 },
            { fluid = 'Waste Liquid',                circuit = 3, weight = 0 },
        }
    }
}
