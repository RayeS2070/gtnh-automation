---The ME network in the protocol's shape. A stack from OC is heavy (binary NBT, patterns, cell
---and energy fields): only the fields the server needs are copied, never the whole stack.
local me = {}

---@class fc.Item
---@field name string
---@field damage integer
---@field label string
---@field size number
---@field fluid string? # fluidDrop.name of a fluid drop: the server keys drops by it
---@field craftable boolean? # scanned items: AE has a pattern for the item

---@param stack table # an ItemStack from OC
---@return fc.Item
function me.item(stack)
    return {
        name = stack.name,
        damage = stack.damage or 0,
        label = stack.label or '',
        size = stack.size or 0,
        fluid = stack.fluidDrop and stack.fluidDrop.name or nil,
    }
end

---The item a stack belongs to, as the server keys it: a fluid drop by its fluid.
---@param stack table
---@return string
function me.key(stack)
    if stack.fluidDrop then
        return 'fluid:' .. tostring(stack.fluidDrop.name)
    end
    return ('%s:%d'):format(stack.name, stack.damage or 0)
end

---Whether an item (its stacks summed) goes to the server: enough of it, or craftable (orders need
---it even when nothing is stored).
---@param item { size: number?, isCraftable: boolean? }
---@param threshold integer
function me.keep(item, threshold)
    return (item.size or 0) >= threshold or item.isCraftable == true
end

---Ids first..last in one getItemsInNetworkById call, as items worth sending.
---@param proxy table # me_interface
---@return fc.Item[]? items
---@return string? err
function me.chunk(proxy, first, last, threshold)
    local ids = {}
    for id = first, last do
        ids[#ids + 1] = id
    end
    local ok, stacks = pcall(proxy.getItemsInNetworkById, ids)
    if not ok then
        return nil, ('getItemsInNetworkById(%d..%d): %s'):format(first, last, tostring(stacks))
    end
    -- Kept or left out per item, not per stack: one item may come as several stacks (in game:
    -- a stored MFE with NBT, not craftable, next to its craftable entry of size 0). Deciding per
    -- stack sent only the 0.
    local sums = {}
    for _, s in ipairs(stacks or {}) do
        local k = me.key(s)
        local sum = sums[k] or { size = 0, isCraftable = false }
        sum.size = sum.size + (s.size or 0)
        sum.isCraftable = sum.isCraftable or s.isCraftable == true
        sums[k] = sum
    end
    local items = {}
    for _, s in ipairs(stacks or {}) do
        local sum = sums[me.key(s)]
        if me.keep(sum, threshold) then
            local item = me.item(s)
            item.craftable = sum.isCraftable
            items[#items + 1] = item
        end
    end
    return items
end

---@class fc.Cpu
---@field name string
---@field busy boolean
---@field output fc.Item?

---Every CPU with its finalOutput(). Asked of every CPU, not only of busy ones: in game
---getCpus() once said busy=false while finalOutput() already showed a craft just started.
---@param proxy table
---@return fc.Cpu[]
function me.cpus(proxy)
    local out = {}
    for _, meta in ipairs(proxy.getCpus()) do
        local c = { name = meta.name, busy = meta.busy == true }
        local ok, final = pcall(meta.cpu.finalOutput)
        if ok and final then
            c.output = me.item(final)
        end
        out[#out + 1] = c
    end
    return out
end

return me
