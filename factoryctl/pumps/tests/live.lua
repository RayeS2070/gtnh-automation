---The real pumping agent against a running server, outside the game: HTTP through curl, three
---T1 pump modules and the pumping network faked, time simulated (fast). Checks that both ends
---speak the same protocol. Not installed in game.
---  cd pumps && lua tests/live.lua [http://localhost:8081] [simulated seconds]
package.path = './?.lua;../agent/?.lua;' .. package.path
local pumps = require('fp.pumps')
local server, seconds = arg[1] or 'http://localhost:8081', tonumber(arg[2]) or 75

local function post(path, body)
    local tmp = os.tmpname()
    local f = assert(io.open(tmp, 'w'))
    f:write(body)
    f:close()
    local p = io.popen(('curl -sS -X POST -H "Content-Type: application/json" --data-binary @%s %s%s')
        :format(tmp, server, path))
    local text = p:read('a')
    local ok = p:close()
    os.remove(tmp)
    if not ok then return nil, 'curl failed' end
    print('  <- ' .. text:sub(1, 160))
    return text
end

local now = 1000
local function clock() return now end

local fluids = { oil = 0, helium = 5000000000, neon = 0 }
local modules = {}
local function pump(address)
    local m = { address = address, params = {}, active = false, endsAt = 0 }
    function m.getName() return 'projectmodulepumpt1' end
    function m.setParameter(k, v) m.params[k] = v end
    function m.setWorkAllowed(v)
        if v and not m.active then m.active, m.endsAt = true, now + m.params.batch end
    end
    function m.isMachineActive()
        if m.active and now >= m.endsAt then m.active = false end
        return m.active
    end
    function m.getWorkProgress() return 100 end
    function m.getWorkMaxProgress() return 600 end
    modules[address] = m
end
pump('aaaa0001-live'); pump('aaaa0002-live'); pump('aaaa0003-live')

local saved = nil
local p = assert(pumps.new({
    list = function()
        local out = {}
        for a in pairs(modules) do out[a] = true end
        return out
    end,
    proxy = function(a) return modules[a] end,
    fluids = function()
        local out = {}
        for name, amount in pairs(fluids) do out[#out + 1] = { name = name, amount = amount, size = amount } end
        return out
    end,
    post = post,
    decode = function(text) return load('return ' .. text, '=reply', 't', {})() end,
    store = { read = function() return saved end, write = function(t) saved = t; return true end,
        encode = function() return '{}' end, decode = function() return {} end },
    defaults = { batch = 30, threshold = 0.95, targets = {
        { fluid = 'oil', target = 10000000000, priority = 1 },
        { fluid = 'helium', target = 10000000000, priority = 3 },
        { fluid = 'neon', target = 1000000000, priority = 1 },
    } },
    run = 'live-' .. os.time(),
    warn = function(fmt, ...) print('WRN ' .. fmt:format(...)) end,
    info = function(fmt, ...) print('INF ' .. fmt:format(...)) end,
}, now))

local stop = now + seconds
while now < stop do
    local wait = p:step(now, clock)
    now = now + math.max(wait, 0.05)
end
print(('settings v%d, %d targets'):format(p.settings.version, #p.settings.targets))
