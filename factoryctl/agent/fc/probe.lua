---The stock of the server's targets, measured in every poll by point lookups: getItemInNetwork is
---a hash lookup in AE, cheap. getItemsInNetwork(filter) is not: it converts every stack of the
---network first, on the game server's thread. A fluid drop is found by its NBT {Fluid:"name"}
---(checked in game: tools/stock-probe.lua); an AE fluid (2.9) by getFluidInNetwork(name).
local me = require('fc.me')

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

---Measures every task. Found: its size, and isCraftable if the stack has it; not found: no size
---(0, or a lookup that missed: the server tells them apart by its scan). A lookup that fails is
---logged and left out.
---@param proxy table # me_interface
---@param tasks fc.ProbeTask[]?
---@param warn fun(fmt: string, ...)
---@return table[] # {name, damage, fluid, size, craftable}
function probe.measure(proxy, tasks, warn)
    local out = {}
    for _, t in ipairs(tasks or {}) do
        local ok, stack
        if t.fluid and me.aeFluids(proxy) then
            ok, stack = pcall(proxy.getFluidInNetwork, t.fluid)
        elseif t.fluid then
            ok, stack = pcall(proxy.getItemInNetwork, t.name, 0, probe.nbt(t.fluid))
        else
            ok, stack = pcall(proxy.getItemInNetwork, t.name, t.damage or 0)
        end
        if ok then
            local found = { name = t.name, damage = t.damage, fluid = t.fluid }
            if stack then
                found.size = stack.size or stack.amount
                -- Not `x and y or nil`: that turns false into nil.
                if type(stack.isCraftable) == 'boolean' then
                    found.craftable = stack.isCraftable
                end
            end
            out[#out + 1] = found
        else
            warn('probe %s: %s', t.fluid or (t.name .. ':' .. tostring(t.damage)), tostring(stack))
        end
    end
    return out
end

return probe
