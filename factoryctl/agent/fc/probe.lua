---The stock of the server's targets, measured in every poll by point lookups: getItemInNetwork is
---a hash lookup in AE, cheap. getItemsInNetwork(filter) is not: it converts every stack of the
---network first, on the game server's thread. A fluid drop is found by its NBT {Fluid:"name"}
---(checked in game: tools/stock-probe.lua).
local probe = {}

---@class fc.ProbeTask
---@field name string
---@field damage integer?
---@field fluid string? # a fluid drop's fluid
---@field label string?

---The NBT of a fluid drop, as getItemInNetwork takes it.
---@param fluid string
function probe.nbt(fluid)
    return ('{Fluid:"%s"}'):format(fluid:gsub('[\\"]', '\\%0'))
end

---Measures every task. Found: its size; not found: no size (0, or a lookup that missed: the server
---tells them apart by its scan). A lookup that fails is logged and left out.
---@param proxy table # me_interface
---@param tasks fc.ProbeTask[]?
---@param warn fun(fmt: string, ...)
---@return table[] # {name, damage, fluid, size}
function probe.measure(proxy, tasks, warn)
    local out = {}
    for _, t in ipairs(tasks or {}) do
        local ok, stack
        if t.fluid then
            ok, stack = pcall(proxy.getItemInNetwork, t.name, 0, probe.nbt(t.fluid))
        else
            ok, stack = pcall(proxy.getItemInNetwork, t.name, t.damage or 0)
        end
        if ok then
            out[#out + 1] = { name = t.name, damage = t.damage, fluid = t.fluid,
                size = stack and stack.size or nil }
        else
            warn('probe %s: %s', t.fluid or (t.name .. ':' .. tostring(t.damage)), tostring(stack))
        end
    end
    return out
end

return probe
