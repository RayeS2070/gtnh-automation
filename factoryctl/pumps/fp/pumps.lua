---The pumping agent: finds the pump modules, starts their cycles by the plan, reports to the
---factoryctl server and takes its settings. It decides by itself: without the server it keeps
---the levels of the last settings it had (saved), or of config.lua's defaults.
---
---step(now) does whatever is due and returns how long the caller may wait in event.pull: no
---sleeps here, so Ctrl+C and every module's next step come on time.
local json = require('fc.json')
local module = require('fp.module')
local plan = require('fp.plan')
local recipes = require('fp.recipes')
local settings = require('fp.settings')

local pumps = {}
pumps.__index = pumps

local kDiscover = 30  -- s between looks for modules added or taken away
local kIdle = 10      -- s before an idle module with nothing to pump looks again
local kStockTtl = 2   -- s a read of the network serves modules that start together
local kRetry = 10     -- s before polling again after a failed poll
local kPoll = 10      -- s between polls until the server says

---@class fp.Deps
---@field list fun(): table<string, boolean> # addresses of gt_machine components
---@field proxy fun(address: string): table
---@field fluids fun(): table[] # getFluidsInNetwork() of the pumping network
---@field post fun(path: string, body: string): string?, string?
---@field decode fun(text: string): any
---@field store fp.Store
---@field defaults table # config.lua's settings
---@field run string # this run of the agent: cycles are told apart by (run, seq)
---@field warn fun(fmt: string, ...)?
---@field info fun(fmt: string, ...)?
---@field logs table? # fc.logbuf

---@param deps fp.Deps
---@param now number
---@return table? agent
---@return string? err
function pumps.new(deps, now)
    local p = setmetatable({}, pumps)
    p.deps = deps
    p.warn = deps.warn or function() end
    p.info = deps.info or function() end
    local s, from, err = settings.load(deps.store, deps.defaults, p.warn)
    if not s then
        return nil, err
    end
    p.settings = s
    p.info('settings: %s, %d targets, batch %d s', from, #s.targets, s.batch)
    p.modules = {}  -- address -> fp module
    p.ignored = {}  -- gt_machine addresses that are no pump modules
    p.seq = 0
    p.ended = {}    -- cycles not confirmed by the server yet
    p.stock = nil   -- { at, map } of the last read
    p.nextDiscover = now
    p.nextPoll = now
    p.interval = kPoll
    p.recipesSent = false
    return p
end

function pumps:discover(now)
    local present = self.deps.list()
    for address in pairs(present) do
        if not self.modules[address] and not self.ignored[address] then
            local proxy = self.deps.proxy(address)
            local ok, name = pcall(proxy.getName)
            local kind = ok and module.kTiers[name]
            if kind then
                self.modules[address] = module.new(proxy, kind, now, self.warn)
                self.info('pump module T%d %s', kind.tier, address:sub(1, 8))
            elseif ok then
                self.ignored[address] = true
            end -- a failed getName: asked again next time
        end
    end
    for address, m in pairs(self.modules) do
        if not present[address] then
            self.modules[address] = nil
            self.warn('pump module %s is gone', address:sub(1, 8))
            if m.cycle and not m.cycle.unknown then
                m.cycle.result, m.cycle.message = 'failed', 'module gone'
                self.ended[#self.ended + 1] = self:record(m, m.cycle)
            end
        end
    end
    self.nextDiscover = now + kDiscover
end

---Fluid -> mB in the pumping network; nil when the read fails.
function pumps:read(now)
    if self.stock and now - self.stock.at < kStockTtl then
        return self.stock.map
    end
    local ok, list = pcall(self.deps.fluids)
    if not ok or type(list) ~= 'table' then
        self.warn('getFluidsInNetwork: %s', tostring(list))
        return nil
    end
    local map = {}
    for _, f in ipairs(list) do
        if f.name then
            map[f.name] = (map[f.name] or 0) + (f.size or f.amount or 0)
        end
    end
    self.stock = { at = now, map = map }
    return map
end

---The network plus what running cycles will bring, and the fluids they pump: two modules never
---fill one shortage blindly.
---@return table<string, number>? have
---@return table<string, boolean> busy
function pumps:have(now)
    local map = self:read(now)
    local busy = {}
    if not map then
        return nil, busy
    end
    local have = {}
    for fluid, amount in pairs(map) do
        have[fluid] = amount
    end
    for _, m in pairs(self.modules) do
        local c = m.cycle
        if c and not c.unknown and not c.result then
            for _, s in ipairs(c.slots) do
                have[s.fluid] = (have[s.fluid] or 0) + plan.expected(s, c.batch)
                busy[s.fluid] = true
            end
        end
    end
    return have, busy
end

---A cycle as the server takes it.
function pumps:record(m, c)
    local slots = {}
    for i, s in ipairs(c.slots) do
        slots[i] = { fluid = s.fluid, parallel = s.parallel, expected = plan.expected(s, c.batch) }
    end
    return { run = self.deps.run, seq = c.seq, module = m.address, batch = c.batch,
        result = c.result, message = c.message, duration = c.duration, slots = slots }
end

local function sortedModules(self)
    local list = {}
    for _, m in pairs(self.modules) do
        list[#list + 1] = m
    end
    table.sort(list, function(a, b) return a.address < b.address end)
    return list
end

function pumps:stepModules(now)
    for _, m in ipairs(sortedModules(self)) do
        local ended = m:step(now)
        if ended then
            self.stock = nil -- what it pumped is in the network now
            self.ended[#self.ended + 1] = self:record(m, ended)
            if ended.result == 'failed' then
                self.warn('pump %s: cycle %d failed: %s', m.address:sub(1, 8), ended.seq,
                    tostring(ended.message))
            end
        end
        if m.state == 'idle' and now >= m.at then
            local have, busy = self:have(now)
            local slots = have
                and plan.slots(plan.low(have, self.settings), m.slots, m.parallels, busy)
            if not slots or #slots == 0 then
                m.at = now + kIdle
            else
                self.seq = self.seq + 1
                local c = m:start(slots, self.settings.batch, self.seq, now)
                if not c then
                    self.ended[#self.ended + 1] = self:record(m, m.cycle)
                    self.warn('pump %s: %s', m.address:sub(1, 8), tostring(m.cycle.message))
                end
            end
        end
    end
end

---The modules for the server and the web.
function pumps:snapshot()
    local out = {}
    for _, m in ipairs(sortedModules(self)) do
        local slots = {}
        if m.cycle and not m.cycle.unknown then
            for i, s in ipairs(m.cycle.slots) do
                slots[i] = { fluid = s.fluid, parallel = s.parallel }
            end
        end
        local done, max = nil, nil
        if m.state == 'running' then
            done, max = m:progress()
        end
        out[#out + 1] = { address = m.address, tier = m.tier, state = m.state, slots = slots,
            progress = done, maxProgress = max }
    end
    return out
end

---The stock of every target fluid, as last read.
function pumps:targetStock(now)
    local map = self:read(now) or {}
    local out = {}
    for _, t in ipairs(self.settings.targets) do
        out[#out + 1] = { fluid = t.fluid, amount = map[t.fluid] or 0 }
    end
    return out
end

function pumps:poll(now)
    local lines = self.deps.logs and self.deps.logs:entries() or {}
    local body = {
        run = self.deps.run,
        settings = self.settings.version,
        modules = self:snapshot(),
        cycles = self.ended,
        stock = self:targetStock(now),
        log = lines,
    }
    -- The server has none yet: it takes these, so config.lua's targets need no typing in.
    if self.settings.version == 0 then
        body.offer = settings.plain(self.settings)
    end
    if not self.recipesSent then
        local list = {}
        for i, r in ipairs(recipes.list) do
            list[i] = { planet = r.planet, gas = r.gas, fluid = r.fluid, label = r.label,
                rate = r.rate }
        end
        body.recipes = list
    end
    local sent = #self.ended
    local text, err = self.deps.post('/agent/v1/pumps/poll', json.encode(body))
    local ok, reply = false, nil
    if text then
        ok, reply = pcall(self.deps.decode, text)
    end
    if not text or not ok or type(reply) ~= 'table' or reply.ok == false then
        local why = err or (ok and type(reply) == 'table' and reply.error) or text
        self.warn('poll: %s', tostring(why))
        self.nextPoll = now + kRetry
        return false
    end
    -- Cycles that ended while the request was out stay for the next poll.
    table.move(self.ended, sent + 1, #self.ended, 1)
    for i = #self.ended - sent + 1, #self.ended do
        self.ended[i] = nil
    end
    if self.deps.logs then
        self.deps.logs:confirm()
    end
    self.recipesSent = true
    if reply.settings then
        local s, serr = settings.check(reply.settings, self.warn)
        if not s then
            self.warn('settings from the server refused: %s', tostring(serr))
        elseif s.version ~= self.settings.version then
            self.settings = s
            local saved, werr = settings.save(self.deps.store, s)
            if not saved then
                self.warn('settings not saved: %s', tostring(werr))
            end
            self.info('settings v%d from the server: %d targets, batch %d s', s.version,
                #s.targets, s.batch)
        end
    end
    self.interval = math.tointeger(reply.poll) or self.interval
    self.nextPoll = now + self.interval
    return true
end

---On exit: nothing is left allowed to work. A cycle that runs ends by itself.
function pumps:stop()
    for _, m in pairs(self.modules) do
        m:call('setWorkAllowed', false)
    end
end

---Does what is due at `now`; returns the seconds the caller may wait.
---@param now number
---@param clock fun(): number # the time after slow work (a poll takes a while)
function pumps:step(now, clock)
    if now >= self.nextDiscover then
        self:discover(now)
    end
    self:stepModules(now)
    -- Never during a pulse: a module left allowed to work for the length of a request could take
    -- a second cycle. A pulse is 0.1 s; the poll waits for it.
    local pulseEnd = nil
    for _, m in pairs(self.modules) do
        if m.state == 'pulse' then
            pulseEnd = math.max(pulseEnd or m.at, m.at)
        end
    end
    if now >= self.nextPoll and not pulseEnd then
        self:poll(now)
        now = clock()
        self:stepModules(now)
    end
    local wake = math.min(math.max(self.nextPoll, pulseEnd or 0), self.nextDiscover)
    for _, m in pairs(self.modules) do
        wake = math.min(wake, m.at)
    end
    return math.max(0, wake - now)
end

return pumps
