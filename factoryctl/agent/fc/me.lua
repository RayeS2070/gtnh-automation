---The ME network in the protocol's shape. A stack from OC is heavy (binary NBT, patterns, cell
---and energy fields): only the fields the server needs are copied, never the whole stack.
local me = {}

---@class fc.Item
---@field name string
---@field damage integer
---@field label string
---@field size number
---@field fluid string? # fluidDrop.name of a fluid drop: the server keys drops by it

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

---Whether a scanned stack goes to the server: enough of it, or craftable (orders need it even
---when nothing is stored).
---@param stack table
---@param threshold integer
function me.keep(stack, threshold)
    return (stack.size or 0) >= threshold or stack.isCraftable == true
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
    local items = {}
    for _, s in ipairs(stacks or {}) do
        if me.keep(s, threshold) then
            items[#items + 1] = me.item(s)
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
