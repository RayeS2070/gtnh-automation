---The session state on the computer's disk: the fallback for when the server is out of reach at
---a restart. Written once a second while a hole is open; the cost of a write is measured.
local serialization = require('serialization')

local store = {}

store.defaultPath = '/home/factoryctl/bhc/state.dat'

-- os.time() in OC is world time in game seconds: 1 tick = 3.6 of them. It stops with the
-- daylight cycle, so it only counts when it moved.
local kGameSecondsPerSecond = 72

---@class bhc.Saved
---@field state bhc.State
---@field uptime number # computer.uptime() at the save
---@field time number # os.time() at the save

---@param path string
---@return bhc.Saved? saved # nil: no file
---@return string? err
function store.load(path)
    local f = io.open(path, 'r')
    if not f then
        return nil
    end
    local text = f:read('*a')
    f:close()
    local data = serialization.unserialize(text)
    if type(data) ~= 'table' or type(data.state) ~= 'table' then
        return nil, 'state file is corrupt: ' .. path
    end
    return data
end

---@param path string
---@param saved bhc.Saved
---@return boolean? ok
---@return string? err
function store.save(path, saved)
    local tmp = path .. '.new'
    local f, err = io.open(tmp, 'w')
    if not f then
        return nil, err
    end
    f:write(serialization.serialize(saved))
    f:close()
    -- A rename keeps the old file whole if the computer stops mid-write.
    local fs = require('filesystem')
    fs.remove(path)
    local ok, reason = fs.rename(tmp, path)
    if not ok then
        return nil, reason
    end
    return true
end

---Seconds of machine time since the save, or nil if they cannot be known.
---@param saved bhc.Saved
---@param uptime number
---@param time number
---@return number?
function store.elapsed(saved, uptime, time)
    if uptime >= saved.uptime then
        return uptime - saved.uptime -- the same boot: uptime runs with the world's ticks
    end
    if time > saved.time then
        return (time - saved.time) / kGameSecondsPerSecond
    end
    return nil
end

---Running cost of the writes, for the log.
---@class bhc.WriteCost
---@field count integer
---@field total number # s of uptime
---@field worst number

---@return bhc.WriteCost
function store.newCost()
    return { count = 0, total = 0, worst = 0 }
end

---@param cost bhc.WriteCost
---@param took number
function store.account(cost, took)
    cost.count = cost.count + 1
    cost.total = cost.total + took
    if took > cost.worst then
        cost.worst = took
    end
end

return store
