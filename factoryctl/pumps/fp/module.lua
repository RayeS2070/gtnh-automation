---One Space Elevator pump module and its cycles. A cycle is started by a pulse of work allowed:
---true, two ticks later false; the module then runs that one recipe (batch seconds) and stops.
---Never a blocking wait: step() does what is due now and says when it wants to be called again.
---
---  idle --start()--> pulse --0.1 s--> starting --active--> running --inactive--> idle
---                                        \--not active in kStartGrace--> backoff --> idle
---
---Every component call is under pcall: a module that fails a call goes to backoff with the error
---in its cycle's result, the agent goes on.
local module = {}
module.__index = module

-- TileEntityModulePump: recipe slots and parallels of each slot, by getName().
module.kTiers = {
    projectmodulepumpt1 = { tier = 1, slots = 1, parallels = 4 },
    projectmodulepumpt2 = { tier = 2, slots = 4, parallels = 4 },
    projectmodulepumpt3 = { tier = 3, slots = 4, parallels = 64 },
}

local kPulse = 0.1       -- s of work allowed: setWorkAllowed takes effect the next tick
local kStartGrace = 2    -- s for a pulsed module to show it runs
local kStartCheck = 0.5  -- s between looks while starting
local kLate = 30         -- s past its batch a running cycle is warned of
local kRunCheck = 1      -- s between looks once a cycle should be over
local kStuckCheck = 5    -- s between looks at a late cycle
local kBackoff = 30      -- s before a module that failed is tried again

---@class fp.Cycle
---@field seq integer
---@field batch integer
---@field slots fp.Slot[]
---@field startedAt number
---@field result string? # 'done' | 'failed'
---@field duration number? # s from the pulse to the stop, for done ones
---@field late boolean? # warned of
---@field message string?

---@param proxy table # gt_machine
---@param kind { tier: integer, slots: integer, parallels: integer }
---@param now number
---@param warn fun(fmt: string, ...)?
function module.new(proxy, kind, now, warn)
    local m = setmetatable({
        warn = warn or function() end,
        proxy = proxy,
        address = proxy.address,
        tier = kind.tier,
        slots = kind.slots,
        parallels = kind.parallels,
        state = 'idle',
        at = now,
        cycle = nil, ---@type fp.Cycle?
    }, module)
    -- One cycle at a time, ours: the old script may have left it allowed to work.
    local ok, active = m:call('setWorkAllowed', false)
    if ok then
        ok, active = m:call('isMachineActive')
    end
    if ok and active then
        -- A cycle from before (another run of the agent): wait for it, know nothing of it.
        m.state, m.at = 'running', now + kRunCheck
        m.cycle = { seq = 0, batch = 0, slots = {}, startedAt = now, unknown = true }
    end
    return m
end

---@return boolean ok
---@return any value_or_error
function module:call(method, ...)
    local f = self.proxy[method]
    if not f then
        return false, 'no method ' .. method
    end
    return pcall(f, ...)
end

---Starts a cycle on these slots. Only when idle and due.
---@param slots fp.Slot[]
---@param batch integer
---@param seq integer
---@param now number
---@return fp.Cycle? # nil: the module could not be set up (it is in backoff, see `failed`)
function module:start(slots, batch, seq, now)
    local cycle = { seq = seq, batch = batch, slots = slots, startedAt = now }
    self.cycle = cycle
    local ok, err = self:call('setParameter', 'batch', batch)
    for i = 0, self.slots - 1 do
        local s = slots[i + 1]
        if not ok then break end
        if s then
            ok, err = self:call('setParameter', ('recipe%d.planetType'):format(i), s.planet)
            if ok then ok, err = self:call('setParameter', ('recipe%d.gasType'):format(i), s.gas) end
            if ok then
                ok, err = self:call('setParameter', ('recipe%d.parallel'):format(i), s.parallel)
            end
        else
            -- A slot with nothing to pump: no parallels, no energy spent on it.
            ok, err = self:call('setParameter', ('recipe%d.parallel'):format(i), 0)
        end
    end
    if ok then
        ok, err = self:call('setWorkAllowed', true)
    end
    if not ok then
        self:fail(now, 'setting up: ' .. tostring(err))
        return nil
    end
    self.state, self.at = 'pulse', now + kPulse
    return cycle
end

function module:fail(now, message)
    self:call('setWorkAllowed', false)
    if self.cycle then
        self.cycle.result, self.cycle.message = 'failed', message
    end
    self.state, self.at = 'backoff', now + kBackoff
end

---Does what is due. Returns the cycle that ended now, if one did: done, or failed. A cycle late
---by kLate is a warning (once), and still ends as done when the module stops.
---@param now number
---@return fp.Cycle?
function module:step(now)
    if now < self.at then
        return nil
    end
    if self.state == 'pulse' then
        local ok, err = self:call('setWorkAllowed', false)
        if not ok then
            self:fail(now, 'setWorkAllowed: ' .. tostring(err))
            return self.cycle
        end
        self.state, self.at = 'starting', now
        return self:step(now)
    elseif self.state == 'starting' then
        local ok, active = self:call('isMachineActive')
        if not ok then
            self:fail(now, 'isMachineActive: ' .. tostring(active))
            return self.cycle
        end
        if active then
            self.state = 'running'
            self.at = self.cycle.startedAt + self.cycle.batch
            return nil
        end
        if now - self.cycle.startedAt >= kStartGrace then
            -- No energy, no output space, a slot AE does not take: the module says why in its
            -- GUI; OC does not.
            self:fail(now, 'did not start')
            return self.cycle
        end
        self.at = now + kStartCheck
        return nil
    elseif self.state == 'running' then
        local ok, active = self:call('isMachineActive')
        if not ok then
            self:fail(now, 'isMachineActive: ' .. tostring(active))
            return self.cycle
        end
        local cycle = self.cycle
        if not active then
            self.state, self.at, self.cycle = 'idle', now, nil
            if cycle.unknown then
                return nil
            end
            cycle.result = 'done'
            cycle.duration = now - cycle.startedAt
            return cycle
        end
        local late = not cycle.unknown and now >= cycle.startedAt + cycle.batch + kLate
        if late and not cycle.late then
            cycle.late = true
            self.warn('pump %s: cycle %d still runs %d s after its start (batch %d s)',
                self.address:sub(1, 8), cycle.seq, math.floor(now - cycle.startedAt), cycle.batch)
        end
        self.at = now + (late and kStuckCheck or kRunCheck)
        return nil
    elseif self.state == 'backoff' then
        self.state, self.at, self.cycle = 'idle', now, nil
    end
    return nil
end

---Work progress for the server and the web: ticks done and in all.
function module:progress()
    local ok, done = self:call('getWorkProgress')
    local okMax, max = self:call('getWorkMaxProgress')
    if ok and okMax and type(done) == 'number' and type(max) == 'number' then
        return done, max
    end
    return nil, nil
end

return module
