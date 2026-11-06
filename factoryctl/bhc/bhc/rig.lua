---The hardware around the black hole: finds the components, measures and carries out what the
---state machine asks. Everything it touches comes in through `deps`, so tests run it on fakes.
---
---  controller   gt_machine at the BHC controller (its getName() has "blackhole")
---  redstone     Redstone I/O next to the Utility Hatch (pulse mode)
---  transposer   touches the supplies interface (items), the supplies tank, the catalyst
---               input bus and a spacetime hatch
---  ingredients  ME component of the ingredients subnet: is there work
---  supplies     ME component of the supplies subnet: seeds, collapsers, spacetime
local network = require('fc.network')

local rig = {}
rig.__index = rig

-- gregtech:gt.metaitem.01 = 32000 + IDMetaItem01 (418 opener, 419 closer); check with bhc-dump.
rig.kSeed = { name = 'gregtech:gt.metaitem.01', damage = 32418 }
rig.kCollapser = { name = 'gregtech:gt.metaitem.01', damage = 32419 }
rig.kSpacetime = 'molten.spacetime'

---@class bhc.Deps
---@field list fun(kind: string): table<string, string>
---@field proxy fun(address: string): table
---@field config table
---@field clock fun(): number

local function prefixed(list, kind, prefix)
    local found = {}
    for address in pairs(list(kind)) do
        if not prefix or address:sub(1, #prefix) == prefix then
            found[#found + 1] = address
        end
    end
    table.sort(found)
    return found
end

local function one(list, kind, prefix, what)
    local found = prefixed(list, kind, prefix)
    if #found == 1 then
        return found[1]
    end
    error(('%s: %d components %s%s, give its address in config.lua'):format(what, #found, kind,
        prefix and (' at ' .. prefix) or ''), 0)
end

---Finds the components; errors with a message for the user when something is missing.
---@param deps bhc.Deps
function rig.new(deps)
    local c = deps.config
    local self = setmetatable({ deps = deps, config = c }, rig)

    local controllers = {}
    for _, address in ipairs(prefixed(deps.list, 'gt_machine', c.controller)) do
        local p = deps.proxy(address)
        local ok, name = pcall(p.getName)
        if ok and type(name) == 'string' and name:find('blackhole', 1, true) then
            controllers[#controllers + 1] = p
        end
    end
    if #controllers ~= 1 then
        error(('%d BHC controllers found: give the adapter address in config.lua')
            :format(#controllers), 0)
    end
    self.controller = controllers[1]
    self.redstone = deps.proxy(one(deps.list, 'redstone', c.redstone, 'redstone'))
    self.transposer = deps.proxy(one(deps.list, 'transposer', c.transposer, 'transposer'))

    for _, role in ipairs({ 'ingredients', 'supplies' }) do
        if not c[role] then
            error(('config.lua: %s must be an address prefix: two subnets look alike'):format(role), 0)
        end
        local address, kind = network.find(deps.list, c[role])
        if not address then
            error(role .. ': ' .. kind, 0)
        end
        self[role .. 'Me'] = deps.proxy(address)
    end
    for _, side in ipairs({ 'pulse', 'supplies', 'tank', 'bus', 'hatch' }) do
        if type(c.sides) ~= 'table' or type(c.sides[side]) ~= 'number' then
            error(('config.lua: sides.%s is not set (bhc-dump shows the sides)'):format(side), 0)
        end
    end
    self.sides = c.sides
    self.workCheckedAt = nil
    self.work = false
    return self
end

local function same(stack, want)
    return type(stack) == 'table' and stack.name == want.name
        and (want.damage == nil or stack.damage == want.damage)
end

---Makes the supplies interface stock seeds, collapsers and spacetime (config.stock). Leaves a
---slot that holds something else alone. Returns false and why when it could not.
---@param log table
---@return boolean, string?
function rig:stock(log)
    local me, c = self.suppliesMe, self.config.stock
    if not c then
        return true
    end
    if not (me.setInterfaceConfiguration and me.setFluidInterfaceConfiguration) then
        return false, 'the supplies component is not an interface: stock it by hand'
    end
    local items = {
        { what = 'seed', want = rig.kSeed, slot = c.seedSlot, size = c.seeds },
        { what = 'collapser', want = rig.kCollapser, slot = c.collapserSlot,
            size = c.collapsers },
    }
    for _, it in ipairs(items) do
        local now = me.getInterfaceConfiguration(it.slot)
        if same(now, it.want) and math.floor(now.size or 0) == it.size then
            -- as it should be
        elseif type(now) == 'table' and now.name and not same(now, it.want) then
            return false, ('interface slot %d stocks %s, not %ss: free it or change stock')
                :format(it.slot, tostring(now.label or now.name), it.what)
        else
            me.setInterfaceConfiguration(it.slot, { name = it.want.name, damage = it.want.damage,
                size = it.size })
            log.inf('interface slot %d now stocks %d %ss', it.slot, it.size, it.what)
        end
    end
    local name = self.config.spacetime or rig.kSpacetime
    local now = me.getFluidInterfaceConfiguration(c.spacetimeSlot)
    if same(now, { name = name }) then
        return true
    elseif type(now) == 'table' and now.name then
        return false, ('fluid slot %d stocks %s, not spacetime: free it or change stock')
            :format(c.spacetimeSlot, tostring(now.label or now.name))
    end
    -- throws "fluid not found" when the name is wrong
    me.setFluidInterfaceConfiguration(c.spacetimeSlot, { name = name })
    log.inf('interface fluid slot %d now stocks %s', c.spacetimeSlot, name)
    return true
end

---Is this redstone_changed signal the pulse's rising edge?
function rig:isPulse(address, side, old, new)
    return address == self.redstone.address and side == self.sides.pulse and new > 0 and old == 0
end

function rig:level()
    local n = self.transposer.getTankLevel(self.sides.hatch)
    return math.floor(n or 0)
end

function rig:busy()
    return (self.controller.getWorkMaxProgress() or 0) > 0
end

---Anything in the ingredients subnet; read at most every `workEvery` s.
function rig:hasWork(now)
    if self.workCheckedAt and now - self.workCheckedAt < self.config.workEvery then
        return self.work
    end
    self.workCheckedAt = now
    local items = self.ingredientsMe.getItemsInNetwork() or {}
    local fluids = self.ingredientsMe.getFluidsInNetwork and self.ingredientsMe.getFluidsInNetwork()
        or {}
    local work = false
    for _, stack in ipairs(items) do
        if (stack.size or 0) > 0 then
            work = true
            break
        end
    end
    for _, stack in ipairs(fluids) do
        if (stack.amount or 0) > 0 then
            work = true
            break
        end
    end
    self.work = work
    return work
end

function rig:supplies()
    local function size(stack)
        return stack and math.floor(stack.size or 0) or 0
    end
    local fluid = self.suppliesMe.getFluidInNetwork(self.config.spacetime or rig.kSpacetime)
    return {
        seeds = size(self.suppliesMe.getItemInNetwork(rig.kSeed.name, rig.kSeed.damage)),
        collapsers = size(self.suppliesMe.getItemInNetwork(rig.kCollapser.name,
            rig.kCollapser.damage)),
        spacetime = fluid and math.floor(fluid.amount or 0) or 0,
    }
end

local function isCatalyst(stack)
    return stack and (stack.size or 0) > 0 and stack.name == rig.kSeed.name
        and (stack.damage == rig.kSeed.damage or stack.damage == rig.kCollapser.damage)
end

---A seed or collapser in the bus; anything else there (a circuit) is not ours.
function rig:catalyst()
    local t, side = self.transposer, self.sides.bus
    for slot = 1, t.getInventorySize(side) or 0 do
        if isCatalyst(t.getStackInSlot(side, slot)) then
            return true
        end
    end
    return false
end

---What the state machine sees now. Costly reads only where the phase needs them.
---@param pulse boolean
---@param phase bhc.Phase
---@return bhc.Observation
function rig:observe(pulse, phase)
    local now = self.deps.clock()
    local obs = { now = now, pulse = pulse, level = self:level(), busy = self:busy() }
    obs.work = self:hasWork(now)
    if phase == 'idle' then
        obs.catalyst = self:catalyst()
        if obs.work and not obs.catalyst then
            obs.supplies = self:supplies()
        end
    elseif phase == 'opening' or phase == 'collapsing' then
        obs.catalyst = self:catalyst()
    else
        obs.catalyst = false
    end
    return obs
end

local function findSlot(t, side, want)
    for slot = 1, t.getInventorySize(side) or 0 do
        local stack = t.getStackInSlot(side, slot)
        if stack and stack.name == want.name and stack.damage == want.damage
            and (stack.size or 0) > 0 then
            return slot
        end
    end
end

---Carries out the actions in order. Returns the warnings of what failed.
---@param actions bhc.Action[]
---@param s bhc.State
---@param hole table # the state machine module (for settle)
---@param log table
function rig:apply(actions, s, hole, log)
    local t, sides = self.transposer, self.sides
    for _, a in ipairs(actions) do
        if a.allow ~= nil then
            self.controller.setWorkAllowed(a.allow)
        elseif a.insert then
            local want = a.insert == 'seed' and rig.kSeed or rig.kCollapser
            local slot = findSlot(t, sides.supplies, want)
            local moved = slot and t.transferItem(sides.supplies, sides.bus, 1, slot) or 0
            if moved ~= 1 then
                log.wrn('no %s in the supplies interface to put in the bus', a.insert)
            end
        elseif a.fill then
            local need = a.fill - self:level()
            if need > 0 then
                t.transferFluid(sides.tank, sides.hatch, need)
            end
            local level = self:level()
            hole.settle(s, level)
            if level < a.fill then
                log.wrn('spacetime hatch holds %d L of %d: the supplies tank is short', level,
                    a.fill)
            end
        elseif a.empty then
            local level = self:level()
            if level > 0 then
                t.transferFluid(sides.hatch, sides.tank, level)
            end
            hole.settle(s, self:level())
        elseif a.clearBus then
            for slot = 1, t.getInventorySize(sides.bus) or 0 do
                local stack = t.getStackInSlot(sides.bus, slot)
                if isCatalyst(stack) then
                    t.transferItem(sides.bus, sides.supplies, stack.size, slot)
                end
            end
        elseif a.warn then
            log.wrn('%s', a.warn)
        elseif a.info then
            log.inf('%s', a.info)
        end
    end
end

return rig
