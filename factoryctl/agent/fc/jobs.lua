---Orders on the agent's side: craft requests, the AE jobs they return, and the results for the
---server. Rules from the game (see factoryctl/AGENTS.md):
---  * a job's state is read as isCanceled, isDone, isComputing, in that order;
---  * hasFailed() counts only before the order was accepted: it turned true in the middle of a
---    craft that went on fine, and after every cancel;
---  * every order is requested at most once; a result waits until a poll got through.
local jobs = {}
jobs.__index = jobs

---@class fc.Task
---@field id integer
---@field amount integer
---@field filter table # goes to getCraftables as is

---@class fc.Result
---@field order integer
---@field status string # computing | accepted | failed | canceled | done
---@field message string?
---@field craftable boolean? # false: getCraftables found nothing (the pattern is gone)

local kFinal = { failed = true, canceled = true, done = true }

---@param warn fun(fmt: string, ...)? # for what goes wrong; silent by default
function jobs.new(warn)
    return setmetatable({
        held = {},    -- order id -> { job, last status, accepted }
        started = {}, -- order ids requested in this run: never twice
        pending = {}, -- results not confirmed yet, the latest one per order
        warn = warn or function() end,
    }, jobs)
end

function jobs:report(order, status, message, craftable)
    for _, r in ipairs(self.pending) do
        if r.order == order then
            r.status, r.message, r.craftable = status, message, craftable
            return
        end
    end
    self.pending[#self.pending + 1] = { order = order, status = status, message = message,
        craftable = craftable }
end

local function flag(job, method)
    local ok, v = pcall(job[method])
    return ok and v == true
end

---The status a job shows now.
local function observe(entry)
    local job = entry.job
    if flag(job, 'isCanceled') then return 'canceled' end
    if flag(job, 'isDone') then return 'done' end
    if flag(job, 'isComputing') then return 'computing' end
    if not entry.accepted and flag(job, 'hasFailed') then return 'failed' end
    entry.accepted = true
    return 'accepted'
end

function jobs:follow(id, entry)
    local status = observe(entry)
    if status == entry.last then
        return
    end
    entry.last = status
    self:report(id, status, status == 'failed' and 'AE could not plan the craft' or nil)
    if kFinal[status] then
        self.held[id] = nil
    end
end

---Runs an order of the server.
---@param proxy table # me_interface
---@param task fc.Task
function jobs:start(proxy, task)
    if self.started[task.id] then
        self.warn('order %d was given twice; ignored', task.id)
        return
    end
    self.started[task.id] = true

    local ok, craftables = pcall(proxy.getCraftables, task.filter)
    if not ok then
        self:report(task.id, 'failed', 'getCraftables: ' .. tostring(craftables))
        return
    end
    if #craftables == 0 then
        self:report(task.id, 'failed', 'not craftable', false)
        return
    elseif #craftables > 1 then
        self:report(task.id, 'failed', ('ambiguous: %d craftables'):format(#craftables))
        return
    end
    local okReq, job = pcall(craftables[1].request, task.amount)
    if not okReq or not job then
        self:report(task.id, 'failed', 'request: ' .. tostring(job))
        return
    end
    local entry = { job = job }
    self.held[task.id] = entry
    self:follow(task.id, entry)
end

---Looks at every held job again.
function jobs:update()
    for id, entry in pairs(self.held) do
        self:follow(id, entry)
    end
end

---@return fc.Result[]
function jobs:results()
    return self.pending
end

---The last poll got through: its results reached the server.
function jobs:confirm()
    self.pending = {}
end

local function matches(stack, filter)
    return stack ~= nil and stack.name == filter.name
        and (filter.damage == nil or stack.damage == filter.damage)
        and (filter.label == nil or stack.label == filter.label)
end

---Cancels the craft of an order on the named CPU, if that CPU still crafts the order's item:
---between the server's view and now the craft may have ended and the CPU taken other work.
---@param proxy table
---@param task { order: integer, cpu: string, filter: table }
function jobs:cancel(proxy, task)
    local ok, cpus = pcall(proxy.getCpus)
    if not ok then
        self.warn('cancel %d: getCpus: %s', task.order, tostring(cpus))
        return
    end
    for _, meta in ipairs(cpus) do
        if meta.name == task.cpu then
            local okOut, final = pcall(meta.cpu.finalOutput)
            if not (okOut and matches(final, task.filter)) then
                self.warn('cancel %d: %s crafts something else now; left alone', task.order,
                    task.cpu)
                return
            end
            local okCancel, done = pcall(meta.cpu.cancel)
            if okCancel and done then
                self.held[task.order] = nil
                self:report(task.order, 'canceled')
            else
                self.warn('cancel %d on %s failed: %s', task.order, task.cpu, tostring(done))
            end
            return
        end
    end
    self.warn('cancel %d: no CPU named %s', task.order, task.cpu)
end

return jobs
