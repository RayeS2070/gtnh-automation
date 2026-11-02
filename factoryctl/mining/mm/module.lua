---One Space Elevator mining module: puts it to what the server assigned and keeps it there.
---Never a blocking wait: step() does what is due now and says when it wants to be called again.
---
---  idle --assign--> stopping --inactive--> loading --set up--> starting --active--> running
---    ^                                       |                    \--not active--> failed
---    \------------- assign(nil): stopping --> unloading (drone back) ---------------/  (retried)
---
---Loading, in order, each piece only when it differs from what is there: the drone (the bus to the
---pool, the interface lends the new one, the pool side to the bus), the drill tip and rod in the
---stocking bus, the plasma in the stocking hatch, the parameters, then work allowed. The drone
---goes through a plain input bus: the module takes none from ME buses, and the controller slot
---allows no automation (GT5U, in game).
---Every component call is under pcall: a module that fails one goes to failed with the error and
---is tried again later; the agent goes on.
local module = {}
module.__index = module

local kDrone = 'gtnhintergalactic:item.MiningDrone'
local kSlot = 1            -- the slot of the drone bus and of the interface used for drones
local kStockingSlots = 16  -- of a stocking bus and hatch (GT5U MTEHatchInputBusME)
local kLend = 10           -- s for the interface to bring the drone from the pool
local kLendCheck = 0.5     -- s between looks at the interface
local kStop = 600          -- s for a running operation to end after work was disallowed
local kStopCheck = 2       -- s between looks while stopping
local kStart = 20          -- s for a module just allowed to work to show it runs
local kStartCheck = 1      -- s between looks while starting
local kRunCheck = 5        -- s between looks while running
local kStall = 30          -- s inactive while running: it stopped (out of tips, plasma, power)
local kRetry = 60          -- s before a failed module is tried again

---@class mm.Assignment
---@field id string
---@field droneTier integer
---@field drone { name: string, damage: integer }
---@field tip { name: string, damage: integer }
---@field rod { name: string, damage: integer }
---@field plasma string
---@field distance integer
---@field parallel integer

---@class mm.Parts
---@field controller table # gt_machine
---@field bus table? # me_inputbus: drill tip and rod
---@field hatch table? # me_inputhatch: plasma
---@field transposer table? # between the drone bus and the pool interface
---@field busSide integer?
---@field poolSide integer?
---@field interface table? # fluid_interface / me_interface of the drone pool

---@param id string # "x,y,z" of the controller
---@param tier integer
---@param parts mm.Parts
---@param now number
---@param warn fun(fmt: string, ...)?
function module.new(id, tier, parts, now, warn)
    local m = setmetatable({
        id = id,
        tier = tier,
        parts = parts,
        warn = warn or function() end,
        state = 'idle',
        at = now,
        want = nil,     ---@type mm.Assignment? # what the server wants
        running = nil,  ---@type mm.Assignment? # what is set up
        written = { bus = {}, hatch = {} }, -- slot -> key: each call costs a tick
        error = nil,
        inactiveSince = nil,
    }, module)
    -- Whatever ran before (another run of the agent) finishes; then it is set up anew.
    local ok, active = m:call(parts.controller, 'isMachineActive')
    if ok and active then
        m:call(parts.controller, 'setWorkAllowed', false)
        m.state, m.at = 'stopping', now + kStopCheck
        m.stopUntil = now + kStop
    end
    return m
end

---@return boolean ok
---@return any value_or_error
function module:call(proxy, method, ...)
    if not proxy then
        return false, 'no component for ' .. method
    end
    local f = proxy[method]
    if not f then
        return false, 'no method ' .. method
    end
    return pcall(f, ...)
end

---The drone tier in the drone bus, nil when none (or no drone bus).
function module:droneInBus()
    local p = self.parts
    if not p.transposer then return nil end
    local ok, stack = self:call(p.transposer, 'getStackInSlot', p.busSide, kSlot)
    if ok and type(stack) == 'table' and stack.name == kDrone then
        return math.tointeger(stack.damage)
    end
    return nil
end

---What the server wants of it now; nil: idle. Takes effect between two operations.
---@param a mm.Assignment?
function module:assign(a, now)
    local same = (a and a.id) == (self.want and self.want.id)
    self.want = a
    if same then return end
    if self.state == 'running' or self.state == 'starting' or self.state == 'failed'
        or (self.state == 'idle' and a) then
        self:stop(now)
    end
end

function module:stop(now)
    self:call(self.parts.controller, 'setWorkAllowed', false)
    self.state, self.at, self.stopUntil = 'stopping', now, now + kStop
end

function module:fail(now, message)
    self:call(self.parts.controller, 'setWorkAllowed', false)
    if message ~= self.error then
        self.warn('mining %s: %s', self.id, message)
    end
    self.error = message
    self.state, self.at = 'failed', now + kRetry
end

local function key(item)
    return item and (item.name .. ':' .. tostring(item.damage)) or ''
end

---Sets a stocking slot unless it holds that already. detail nil clears it.
function module:stock(which, slot, detail, now)
    local proxy = self.parts[which]
    local k = detail and (detail.name .. ':' .. tostring(detail.damage)) or ''
    if self.written[which][slot] == k then return true end
    local ok, err
    if detail then
        ok, err = self:call(proxy, 'setConfiguration', slot, detail)
    else
        ok, err = self:call(proxy, 'setConfiguration', slot)
    end
    if not ok then
        self:fail(now, ('%s slot %d: %s'):format(which, slot, tostring(err)))
        return false
    end
    self.written[which][slot] = k
    return true
end

---Lends a drone of the tier from the pool into the drone bus; the one there goes back first.
---Returns 'done', 'wait' (the interface is bringing it) or nil after a failure.
function module:swapDrone(a, now)
    local p = self.parts
    local have = self:droneInBus()
    if have == a.droneTier then return 'done' end
    if not (p.transposer and p.interface) then
        self:fail(now, 'no drone path: give it one in config.lua (drones)')
        return nil
    end
    if have then
        local ok, moved = self:call(p.transposer, 'transferItem', p.busSide, p.poolSide, 1, kSlot)
        if not ok or moved ~= 1 then
            self:fail(now, 'the drone does not go back to the pool: ' .. tostring(moved))
            return nil
        end
    end
    if not self.lendUntil or self.lendTier ~= a.droneTier then
        local ok, err = self:call(p.interface, 'setInterfaceConfiguration', kSlot,
            { name = kDrone, damage = a.droneTier }, 1)
        if not ok then
            self:fail(now, 'interface: ' .. tostring(err))
            return nil
        end
        self.lendUntil, self.lendTier = now + kLend, a.droneTier
    end
    local ok, stack = self:call(p.transposer, 'getStackInSlot', p.poolSide, kSlot)
    if not (ok and type(stack) == 'table' and stack.name == kDrone
            and math.tointeger(stack.damage) == a.droneTier) then
        if now < self.lendUntil then return 'wait' end
        self.lendUntil = nil
        self:call(p.interface, 'setInterfaceConfiguration', kSlot)
        self:fail(now, ('no %s drone in the pool'):format(a.droneTier))
        return nil
    end
    self.lendUntil = nil
    local moved
    ok, moved = self:call(p.transposer, 'transferItem', p.poolSide, p.busSide, 1, kSlot, kSlot)
    -- Cleared either way: the interface must not bring another one.
    self:call(p.interface, 'setInterfaceConfiguration', kSlot)
    if not ok or moved ~= 1 then
        self:fail(now, 'the drone does not go into the bus: ' .. tostring(moved))
        return nil
    end
    return 'done'
end

---Gives the drone back and clears the stocking slots: an idle module holds nothing.
function module:unload(now)
    local p = self.parts
    if self:droneInBus() then
        local ok, moved = self:call(p.transposer, 'transferItem', p.busSide, p.poolSide, 1, kSlot)
        if not ok or moved ~= 1 then
            self:fail(now, 'the drone does not go back to the pool: ' .. tostring(moved))
            return false
        end
    end
    for slot = 1, 2 do
        if not self:stock('bus', slot, nil, now) then return false end
    end
    return self:stock('hatch', 1, nil, now)
end

function module:load(a, now)
    local swap = self:swapDrone(a, now)
    if swap ~= 'done' then
        if swap == 'wait' then self.at = now + kLendCheck end
        return
    end
    if not (self.parts.bus and self.parts.hatch) then
        self:fail(now, 'no stocking bus or hatch next to it')
        return
    end
    if not (self:stock('bus', 1, a.tip, now) and self:stock('bus', 2, a.rod, now)) then return end
    if not self.cleared then
        for slot = 3, kStockingSlots do
            if not self:stock('bus', slot, nil, now) then return end
        end
        for slot = 2, kStockingSlots do
            if not self:stock('hatch', slot, nil, now) then return end
        end
        self.cleared = true
    end
    if not self:stock('hatch', 1, { name = a.plasma }, now) then return end
    local c = self.parts.controller
    for _, param in ipairs({ { 'distance', a.distance }, { 'parallel', a.parallel },
        { 'cycle', false } }) do
        local ok, err = self:call(c, 'setParameter', param[1], param[2])
        if not ok then
            self:fail(now, ('setParameter %s: %s'):format(param[1], tostring(err)))
            return
        end
    end
    local ok, err = self:call(c, 'setWorkAllowed', true)
    if not ok then
        self:fail(now, 'setWorkAllowed: ' .. tostring(err))
        return
    end
    self.running = a
    self.state, self.at, self.startedAt = 'starting', now + kStartCheck, now
end

---Does what is due; returns when it wants to be called again (an uptime).
function module:step(now)
    if now < self.at then return self.at end
    local c = self.parts.controller
    if self.state == 'stopping' then
        local ok, active = self:call(c, 'isMachineActive')
        if ok and active and now < self.stopUntil then
            self.at = now + kStopCheck
            return self.at
        end
        self.running = nil
        self.state, self.at = self.want and 'loading' or 'unloading', now
        return self:step(now)
    elseif self.state == 'loading' then
        if not self.want then
            self.state = 'unloading'
            return self:step(now)
        end
        self:load(self.want, now)
        if self.state == 'loading' and self.at <= now then self.at = now + kLendCheck end
    elseif self.state == 'unloading' then
        if self:unload(now) then
            self.error = nil
            self.state, self.at = 'idle', now + kRunCheck
        end
    elseif self.state == 'starting' then
        local ok, active = self:call(c, 'isMachineActive')
        if not ok then
            self:fail(now, 'isMachineActive: ' .. tostring(active))
        elseif active then
            self.error, self.inactiveSince = nil, nil
            self.state, self.at = 'running', now + kRunCheck
        elseif now - self.startedAt >= kStart then
            -- No drill tips or plasma in the network, no power, no computation: the module says
            -- why in its GUI; OC does not.
            self:fail(now, 'did not start: tips, rods, plasma, power or computation?')
        else
            self.at = now + kStartCheck
        end
    elseif self.state == 'running' then
        local ok, active = self:call(c, 'isMachineActive')
        if not ok then
            self:fail(now, 'isMachineActive: ' .. tostring(active))
        elseif active then
            self.inactiveSince = nil
            self.at = now + kRunCheck
        else
            -- Between two operations it is inactive for a moment; long, it stopped.
            self.inactiveSince = self.inactiveSince or now
            if now - self.inactiveSince >= kStall then
                self.inactiveSince = nil
                self:fail(now, 'stopped mining: tips, rods, plasma, power or computation?')
            else
                self.at = now + kRunCheck
            end
        end
    elseif self.state == 'failed' then
        self.state, self.at = self.want and 'loading' or 'unloading', now
        return self:step(now)
    elseif self.state == 'idle' then
        if self.want then
            self.state = 'loading'
            return self:step(now)
        end
        self.at = now + kRunCheck
    end
    return self.at
end

---Holds the module (the buffer is full and the server cannot be asked): disallows work, keeps
---the setup; resume() lets it go on.
function module:hold(now)
    if self.state == 'running' or self.state == 'starting' then
        self:call(self.parts.controller, 'setWorkAllowed', false)
        self.state, self.at = 'held', now + kRunCheck
    end
end

function module:resume(now)
    if self.state == 'held' then
        -- The operation that was running when it was held ends first.
        self.state, self.at, self.stopUntil = 'stopping', now, now + kStop
    end
end

---For the server.
function module:report()
    local active = select(2, self:call(self.parts.controller, 'isMachineActive')) == true
    return { state = self.state, active = active, drone = self:droneInBus(),
        assignment = self.running and self.running.id or nil, error = self.error }
end

module.kDrone = kDrone
module.key = key
return module
