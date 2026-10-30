---One step of the agent: report to the server, run what it hands out. The server decides; the
---agent only reads the ME network, requests crafts, cancels them and reports.
local json = require('fc.json')
local jobs = require('fc.jobs')
local me = require('fc.me')
local scan = require('fc.scan')

local agent = {}
agent.__index = agent

---@class fc.AgentDeps
---@field me table # me_interface proxy
---@field post fun(path: string, body: string): string?, string? # HTTP POST to the server
---@field decode fun(text: string): any # a Lua table literal: serialization.unserialize
---@field yield fun()? # between scan chunks; os.sleep(0) in game
---@field warn fun(fmt: string, ...)?
---@field info fun(fmt: string, ...)?

---@param deps fc.AgentDeps
function agent.new(deps)
    local a = setmetatable({}, agent)
    a.me, a.post, a.decode = deps.me, deps.post, deps.decode
    a.yield = deps.yield or function() end
    a.warn = deps.warn or function() end
    a.info = deps.info or function() end
    a.jobs = jobs.new(a.warn)
    return a
end

---POSTs a body and reads the reply table. {ok=false, error=...} is an error too.
function agent:call(path, body)
    local text, err = self.post(path, json.encode(body))
    if not text then
        return nil, err
    end
    local ok, reply = pcall(self.decode, text)
    if not ok or type(reply) ~= 'table' then
        return nil, ('%s: bad reply: %s'):format(path, tostring(ok and text or reply))
    end
    if reply.ok == false then
        return nil, ('%s: %s'):format(path, tostring(reply.error))
    end
    return reply
end

---Runs a scan task: every chunk, the last batch closes the scan. A failed chunk ends the scan;
---the server starts another one later.
function agent:scan(task)
    self.info('scan %d', task.id)
    local sent = 0
    for first, last in scan.chunks(task) do
        local items, err = me.chunk(self.me, first, last, task.threshold)
        if not items then
            self.warn('scan %d stopped: %s', task.id, err)
            return
        end
        local final = last >= task.maxId
        if #items > 0 or final then
            local _, perr = self:call('/agent/v1/scan', { scan = task.id, last = final, items = items })
            if perr then
                self.warn('scan %d stopped: %s', task.id, perr)
                return
            end
            sent = sent + #items
        end
        self.yield()
    end
    self.info('scan %d done: %d stacks', task.id, sent)
end

---One poll. Returns the reply, or nil and why.
function agent:poll()
    self.jobs:update()
    local okCpus, cpus = pcall(me.cpus, self.me)
    if not okCpus then
        -- Report anyway: the results still matter.
        self.warn('getCpus: %s', tostring(cpus))
        cpus = {}
    end
    local reply, err = self:call('/agent/v1/poll', { cpus = cpus, results = self.jobs:results() })
    if not reply then
        return nil, err
    end
    self.jobs:confirm()
    for _, task in ipairs(reply.orders or {}) do
        self.info('order %d: %d x %s', task.id, task.amount, task.filter.label or task.filter.name)
        self.jobs:start(self.me, task)
    end
    for _, task in ipairs(reply.cancel or {}) do
        self.info('cancel order %d on %s', task.order, task.cpu)
        self.jobs:cancel(self.me, task)
    end
    if reply.scan then
        self:scan(reply.scan)
    end
    return reply
end

return agent
