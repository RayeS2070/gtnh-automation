---Local tests of the mining agent with a fake world: a mining module, its stocking bus and hatch,
---the drone bus and transposer, the pool interface, the storage buses, the buffer, the server and
---the clock. Not installed in game.
---  cd factoryctl/mining && lua tests/run.lua
package.path = './?.lua;../agent/?.lua;' .. package.path

local failures, total = 0, 0
local function check(cond, what)
    total = total + 1
    if not cond then
        failures = failures + 1
        print('FAIL ' .. what)
    end
end

local function eq(got, want, what)
    check(got == want, ('%s: got %s, want %s'):format(what, tostring(got), tostring(want)))
end

local suites = {}
local function suite(name, fn)
    suites[#suites + 1] = { name = name, fn = fn }
end

local miner = require('mm.miner')

local world = require('tests.world')
local fakeWorld, run, assignment = world.fakeWorld, world.run, world.assignment
local kOre = world.kOre

suite('discovery: nearest stocking parts, the drone path from the config', function()
    local w = fakeWorld()
    local m = miner.new(w.deps, 0)
    m:discover(0)
    local mod = m.modules['169,33,-36']
    check(mod ~= nil, 'the module is found by its coordinates')
    eq(mod.tier, 2, 'tier from getName')
    eq(mod.parts.bus, w.inputbus, 'the near bus, not the far one')
    eq(mod.parts.hatch, w.inputhatch, 'the hatch')
    eq(mod.parts.transposer, w.transposer, 'transposer by prefix')
    eq(mod.parts.interface, w.interface, 'interface by prefix')
end)

suite('drone path found without config: one module, one transposer', function()
    local w = fakeWorld()
    w.deps.config.drones = nil
    local m = miner.new(w.deps, 0)
    m:discover(0)
    local p = m.modules['169,33,-36'].parts
    eq(p.transposer, w.transposer, 'the transposer')
    eq(p.busSide, 4, 'bus side from what it holds')
    eq(p.poolSide, 0, 'pool side from what it holds')
    eq(p.interface, w.interface, 'the only interface')
end)

suite('drone path: spaces in the key, sides left out', function()
    local w = fakeWorld()
    w.deps.config.drones = { ['169, 33, -36'] = { transposer = 'tp-' } }
    local m = miner.new(w.deps, 0)
    m:discover(0)
    local p = m.modules['169,33,-36'].parts
    eq(p.transposer, w.transposer, 'the transposer')
    eq(p.busSide, 4, 'bus side')
    eq(p.poolSide, 0, 'pool side')
end)

suite('config mistakes are told', function()
    local w = fakeWorld()
    w.deps.config.drones = { ['173, 28, -37'] = { transposer = 'tp-' } }
    w.deps.config.pool = '170, 31, -36'
    local m = miner.new(w.deps, 0)
    m:discover(0)
    local text = w.warnings or ''
    check(text:find('drones["173,28,-37"]: no mining module there; the modules are at 169,33,-36',
        1, true), 'a wrong key names the modules: ' .. text)
    check(text:find('pool = "170, 31, -36"', 1, true), 'coordinates as pool')
    -- The transposer is not taken by the wrong key: the module finds it.
    eq(m.modules['169,33,-36'].parts.transposer, w.transposer, 'found anyway')
end)

suite('report: modules, pool, buffer, routing slots', function()
    local w = fakeWorld()
    local m = miner.new(w.deps, 0)
    m:discover(0)
    local body = m:report()
    eq(body.setup, 'mining-1', 'setup')
    eq(#body.modules, 1, 'one module')
    eq(body.modules[1].x, 169, 'x')
    eq(body.modules[1].drone, 5, 'the drone in its bus')
    eq(body.routing.slots, 18, 'the smaller bus')
    local pool = {}
    for _, p in ipairs(body.pool) do pool[p.tier] = p.count end
    eq(pool[9], 1, 'a UEV drone in the pool')
end)

suite('assignment: drone swap, stocking, parameters, running', function()
    local w = fakeWorld()
    w.reply = function()
        return { poll = 10, modules = { assignment(9, 91) }, keep = { kOre }, buffer = { limit = 1000 } }
    end
    local m = miner.new(w.deps, 0)
    run(w, m, 20)
    local mod = m.modules['169,33,-36']
    eq(mod.state, 'running', 'it runs')
    eq(w.droneBus[1] and w.droneBus[1].damage, 9, 'the UEV drone in the bus')
    eq(w.pool[5], 3, 'the LuV drone back in the pool')
    eq(w.pool[9], 0, 'the UEV taken')
    eq(w.bus[1] and w.bus[1].damage, 8009, 'tip')
    eq(w.bus[2] and w.bus[2].damage, 23009, 'rod')
    eq(w.hatch[1] and w.hatch[1].name, 'plasma.radon', 'plasma')
    eq(w.controller.params.distance, 91, 'distance')
    eq(w.controller.params.cycle, false, 'no cycling')
    eq(w.lent, nil, 'the interface lends no more')
    eq(w.storage.slots[1] and w.storage.slots[1].damage, 2982, 'keep bus routes the ore')
    eq(w.void.slots[1] and w.void.slots[1].damage, 2982, 'void bus has the same list')
    check(w.lastBody:find('"assignment":"9/91/3/4"', 1, true), 'reports what it runs')
end)

suite('a new setting waits for the operation to end', function()
    local w = fakeWorld()
    local current = assignment(9, 91)
    w.reply = function() return { poll = 10, modules = { current }, keep = { kOre } } end
    local m = miner.new(w.deps, 0)
    run(w, m, 15)
    local mod = m.modules['169,33,-36']
    eq(mod.state, 'running', 'running')
    local opEnd = w.controller.opEnd
    current = assignment(5, 41)
    run(w, m, 26)
    eq(mod.state, 'stopping', 'stops after its operation')
    eq(w.droneBus[1] and w.droneBus[1].damage, 9, 'the drone stays while it works')
    run(w, m, opEnd + 15)
    eq(w.droneBus[1] and w.droneBus[1].damage, 5, 'the LuV drone now')
    eq(w.controller.params.distance, 41, 'the new distance')
    eq(mod.state, 'running', 'runs again')
end)

suite('idle: the drone goes back, the slots are cleared', function()
    local w = fakeWorld()
    local idle = false
    w.reply = function()
        return { poll = 10, keep = {},
            modules = { idle and { x = 169, y = 33, z = -36, idle = true } or assignment(9, 91) } }
    end
    local m = miner.new(w.deps, 0)
    run(w, m, 15)
    idle = true
    run(w, m, 100)
    local mod = m.modules['169,33,-36']
    eq(mod.state, 'idle', 'idle')
    eq(w.droneBus[1], nil, 'no drone in the bus')
    eq(w.pool[9], 1, 'the UEV drone back')
    eq(w.bus[1], nil, 'no tip')
    eq(w.hatch[1], nil, 'no plasma')
    eq(w.controller.allowed, false, 'not allowed to work')
    -- Nothing kept: an empty bus would take everything, so a placeholder only.
    eq(w.storage.slots[1] and w.storage.slots[1].name, 'minecraft:bedrock', 'keep bus takes nothing')
    eq(w.storage.slots[2], nil, 'the rest cleared')
    eq(w.void.slots[1] and w.void.slots[1].name, 'minecraft:bedrock', 'void bus takes everything')
end)

suite('no drone in the pool: failed, retried', function()
    local w = fakeWorld()
    w.pool[9] = 0
    w.reply = function() return { poll = 10, modules = { assignment(9, 91) }, keep = {} } end
    local m = miner.new(w.deps, 0)
    run(w, m, 15)
    local mod = m.modules['169,33,-36']
    eq(mod.state, 'failed', 'failed')
    check((mod.error or ''):find('no 9 drone'), 'says why: ' .. tostring(mod.error))
    eq(w.lent, nil, 'the interface asks no more')
    w.pool[9] = 1
    run(w, m, 100)
    eq(mod.state, 'running', 'runs once the drone is there')
end)

suite('no tips in the network: did not start', function()
    local w = fakeWorld()
    w.tips = false
    w.reply = function() return { poll = 10, modules = { assignment(9, 91) }, keep = {} } end
    local m = miner.new(w.deps, 0)
    run(w, m, 40)
    local mod = m.modules['169,33,-36']
    eq(mod.state, 'failed', 'failed')
    check((mod.error or ''):find('did not start'), 'says why')
    eq(w.controller.allowed, false, 'disallowed')
end)

suite('server away, buffer full: held; back: goes on', function()
    local w = fakeWorld()
    local up = true
    w.reply = function()
        return { poll = 10, modules = { assignment(9, 91) }, keep = { kOre }, buffer = { limit = 1000 } }
    end
    local reply = w.reply
    local m = miner.new(w.deps, 0)
    run(w, m, 15)
    w.reply = nil -- the server goes away
    run(w, m, 60)
    eq(m.modules['169,33,-36'].state, 'running', 'still runs: the buffer has room')
    w.buffer[kOre.name .. ':' .. kOre.damage] = 5000
    run(w, m, 140)
    eq(m.modules['169,33,-36'].state, 'held', 'held')
    eq(w.controller.allowed, false, 'disallowed')
    w.buffer[kOre.name .. ':' .. kOre.damage] = 10
    w.reply = reply
    run(w, m, 220)
    eq(m.modules['169,33,-36'].state, 'running', 'runs again')
    check(up, 'up')
end)

for _, s in ipairs(suites) do
    local ok, err = pcall(s.fn)
    if not ok then
        failures = failures + 1
        print(('FAIL %s: %s'):format(s.name, err))
    end
end
print(('%d checks, %d failed'):format(total, failures))
os.exit(failures == 0 and 0 or 1)
