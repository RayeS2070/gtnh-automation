---Minimal leveled logger on top of print().
local log = {}

---@enum fc.LogLevel
log.Level = { Dbg = 1, Inf = 2, Wrn = 3, Err = 4 }

local kNames = { 'DBG', 'INF', 'WRN', 'ERR' }
local kShort = { 'dbg', 'inf', 'wrn', 'err' }
local current = log.Level.Inf
local forwardFrom = log.Level.Inf

---Where log lines also go (the agent hands them to the server); nil: nowhere.
---@type fun(level: string, text: string)?
log.forward = nil

---Time source for the prefix; main sets it to computer.uptime, tests leave it as is.
---@type fun(): number?
log.clock = function() return nil end

---What the screen shows.
---@param level fc.LogLevel
function log.setLevel(level)
    current = level
end

---What goes to `forward`, whatever the screen shows.
---@param level fc.LogLevel
function log.setForwardLevel(level)
    forwardFrom = level
end

local function emit(level, fmt, ...)
    if level < current and (not log.forward or level < forwardFrom) then
        return
    end
    local text = fmt:format(...)
    if log.forward and level >= forwardFrom then
        log.forward(kShort[level], text)
    end
    if level >= current then
        local t = log.clock()
        local prefix = t and ('%8.1f %s '):format(t, kNames[level]) or (kNames[level] .. ' ')
        print(prefix .. text)
    end
end

function log.dbg(fmt, ...) emit(log.Level.Dbg, fmt, ...) end
function log.inf(fmt, ...) emit(log.Level.Inf, fmt, ...) end
function log.wrn(fmt, ...) emit(log.Level.Wrn, fmt, ...) end
function log.err(fmt, ...) emit(log.Level.Err, fmt, ...) end

return log
