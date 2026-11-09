---The real mining agent against a running server, outside the game: HTTP through curl, the world
---of tests/world.lua faked, time simulated (fast). Checks that both ends speak the same protocol.
---The plan depends on the server's targets and stock: with none, the module stays idle. Not
---installed in game.
---  cd mining && lua tests/live.lua [http://localhost:8081] [simulated seconds]
package.path = './?.lua;../agent/?.lua;' .. package.path
local miner = require('mm.miner')
local world = require('tests.world')
local server, seconds = arg[1] or 'http://localhost:8081', tonumber(arg[2]) or 60

local function post(path, body)
    local tmp = assert(os.tmpname())
    local f = assert(io.open(tmp, 'w'))
    f:write(body)
    f:close()
    local p = assert(io.popen(('curl -sS -X POST -H "Content-Type: application/json" --data-binary @%s %s%s')
        :format(tmp, server, path)))
    local text = p:read('a')
    local ok = p:close()
    os.remove(tmp)
    if not ok then return nil, 'curl failed' end
    print('  <- ' .. text:sub(1, 200))
    return text
end

local w = world.fakeWorld()
w.deps.post = post
w.deps.warn = function(fmt, ...) print('wrn ' .. fmt:format(...)) end
w.deps.info = function(fmt, ...) print('inf ' .. fmt:format(...)) end
local m = miner.new(w.deps, 0)
world.run(w, m, seconds)
local mod = m.modules['169,33,-36']
print(('after %d s: %s, drone %s, distance %s, routing %d ore(s)'):format(seconds,
    mod.state, tostring(w.droneBus[1] and w.droneBus[1].damage),
    tostring(w.controller.params.distance), #m.keep))
os.exit(mod.error == nil and 0 or 1)
