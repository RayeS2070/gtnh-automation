---Which fluid each slot of a pump module pumps: a pure function of the stock, the settings and
---the module. No component calls here: tested locally (tests/run.lua).
local recipes = require('fp.recipes')

local plan = {}

---@class fp.Target
---@field fluid string
---@field target number # mB to keep; 0: never pumped
---@field priority number # > 0; a greater one is pumped sooner at the same fill
---@field enabled boolean

---@class fp.Settings
---@field version integer # the server's; 0: never had any from it
---@field batch integer # seconds of a cycle, 1..128
---@field threshold number # a fluid below threshold * target is low
---@field targets fp.Target[]

---@class fp.Slot
---@field fluid string
---@field planet integer
---@field gas integer
---@field parallel integer

---How much a fluid is wanted, as the old script ranked it: 1 - fill^priority. 0 when full.
---@param amount number
---@param t fp.Target
function plan.need(amount, t)
    local fill = math.max(amount, 0) / t.target
    if fill >= 1 then
        return 0
    end
    return 1 - fill ^ t.priority
end

---The low fluids, the most wanted first (ties by name: the same input, the same plan).
---@param have table<string, number> # fluid -> mB in the network plus what running cycles bring
---@param settings fp.Settings
---@return { fluid: string, need: number }[]
function plan.low(have, settings)
    local out = {}
    for _, t in ipairs(settings.targets) do
        local amount = have[t.fluid] or 0
        if t.enabled and t.target > 0 and recipes.byFluid[t.fluid]
            and amount < settings.threshold * t.target then
            out[#out + 1] = { fluid = t.fluid, need = plan.need(amount, t) }
        end
    end
    table.sort(out, function(a, b)
        if a.need ~= b.need then
            return a.need > b.need
        end
        return a.fluid < b.fluid
    end)
    return out
end

---Fills the slots of a module: a different low fluid in each, the most wanted first; slots left
---over take the most wanted again, in turn. Fluids other modules pump now come after the rest:
---with targets of 10^10 one cycle hardly moves the ranking, and every module would take the same
---fluid. Nothing low: no slots, the module stays idle.
---@param low { fluid: string }[]
---@param slots integer # recipe slots of the module
---@param parallels integer # parallels of each slot
---@param busy table<string, boolean>? # fluids running cycles pump
---@return fp.Slot[]
function plan.slots(low, slots, parallels, busy)
    local out = {}
    if #low == 0 then
        return out
    end
    local order = {}
    for _, l in ipairs(low) do
        if not (busy and busy[l.fluid]) then order[#order + 1] = l end
    end
    for _, l in ipairs(low) do
        if busy and busy[l.fluid] then order[#order + 1] = l end
    end
    for i = 1, slots do
        local r = recipes.byFluid[order[(i - 1) % #order + 1].fluid]
        out[i] = { fluid = r.fluid, planet = r.planet, gas = r.gas, parallel = parallels }
    end
    return out
end

---mB a slot brings in one cycle.
---@param slot fp.Slot
---@param batch integer
function plan.expected(slot, batch)
    return recipes.byFluid[slot.fluid].rate * slot.parallel * batch
end

return plan
