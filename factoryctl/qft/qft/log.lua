---Minimal leveled logger on top of print().
local log = {}

---@enum qft.LogLevel
log.Level = { Dbg = 1, Inf = 2, Wrn = 3, Err = 4 }

local kNames = { 'DBG', 'INF', 'WRN', 'ERR' }
local current = log.Level.Inf

---Time source for the prefix; main sets it to computer.uptime, tests leave it as is.
---@type fun(): number?
log.clock = function() return nil end

---@param level qft.LogLevel
function log.setLevel(level)
    current = level
end

local function emit(level, fmt, ...)
    if level < current then
        return
    end
    local t = log.clock()
    local prefix = t and ('%8.1f %s '):format(t, kNames[level]) or (kNames[level] .. ' ')
    print(prefix .. fmt:format(...))
end

function log.dbg(fmt, ...) emit(log.Level.Dbg, fmt, ...) end
function log.inf(fmt, ...) emit(log.Level.Inf, fmt, ...) end
function log.wrn(fmt, ...) emit(log.Level.Wrn, fmt, ...) end
function log.err(fmt, ...) emit(log.Level.Err, fmt, ...) end

return log
