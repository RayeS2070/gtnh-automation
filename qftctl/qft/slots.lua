---What the stocking input bus and hatch hold for a recipe (GTNH 2.9: one bus and one hatch serve
---every recipe; the program sets their slots before the recipe starts). Pure: the network comes
---in as a snapshot.
local resource = require('qft.resource')

local slots = {}

slots.kSize = 16 -- slots of a stocking input bus or hatch, basic and advanced alike (in game)

---@class qft.Slots
---@field items { name: string, damage: integer }[] # Bus slots 1.., in the order of the inputs.
---@field fluids { name: string }[] # Hatch slots 1...

---The slots of a recipe, from its inputs and the raw materials network.
---@param recipe qft.Recipe # Inputs carry `resource` (config.normalize).
---@param source qft.Snapshot
---@return qft.Slots? slots
---@return string? err
function slots.forRecipe(recipe, source)
    local out = { items = {}, fluids = {} }
    for _, input in ipairs(recipe.inputs or {}) do
        local detail, err = resource.identify(source, input.resource)
        if not detail then
            return nil, err
        end
        local list = input.resource.kind == 'item' and out.items or out.fluids
        list[#list + 1] = detail
    end
    return out
end

---A slot's content as a comparable string: what is set need not be set again.
---@param detail { name: string, damage: integer? }?
function slots.key(detail)
    if not detail then
        return ''
    end
    return detail.damage and ('%s:%d'):format(detail.name, detail.damage) or detail.name
end

return slots
