---Local tests of the pumping agent with fake pump modules, network, server and clock. Not
---installed in game.
---  cd factoryctl/pumps && lua tests/run.lua
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

-- A Lua literal of plain data, as serialization.serialize gives one; and back.
local function encode(v)
    if type(v) == 'table' then
        local keys = {}
        for k in pairs(v) do keys[#keys + 1] = k end
        table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
        local parts = {}
        for _, k in ipairs(keys) do
            local key = type(k) == 'number' and ('[' .. k .. ']') or ('[%q]'):format(k)
            parts[#parts + 1] = key .. '=' .. encode(v[k])
        end
        return '{' .. table.concat(parts, ',') .. '}'
    elseif type(v) == 'string' then
        return ('%q'):format(v)
    end
    return tostring(v)
end
local function decode(text) return load('return ' .. text, '=data', 't', {})() end

local function nop() end

-- plan ---------------------------------------------------------------------------------------
suite('plan', function()
    local plan = require('fp.plan')
    local s = { version = 0, batch = 30, threshold = 0.95, targets = {
        { fluid = 'oil', target = 1000, priority = 1, enabled = true },
        { fluid = 'helium', target = 1000, priority = 3, enabled = true },
        { fluid = 'lava', target = 1000, priority = 1, enabled = false },
        { fluid = 'argon', target = 0, priority = 1, enabled = true },
        { fluid = 'no.such.fluid', target = 1000, priority = 1, enabled = true },
        { fluid = 'neon', target = 1000, priority = 1, enabled = true },
    } }
    eq(plan.need(0, s.targets[1]), 1, 'empty: wanted most')
    eq(plan.need(1000, s.targets[1]), 0, 'full: not wanted')
    eq(plan.need(2000, s.targets[1]), 0, 'over: not wanted')
    check(plan.need(500, s.targets[2]) > plan.need(500, s.targets[1]),
        'a greater priority is wanted more at the same fill')

    local low = plan.low({ oil = 500, helium = 500, lava = 0, argon = 0, neon = 960 }, s)
    eq(#low, 2, 'only enabled, with a target, a recipe and below the threshold')
    eq(low[1].fluid, 'helium', 'priority 3 first')
    eq(low[2].fluid, 'oil', 'then priority 1')
    eq(plan.low({ oil = 950, helium = 949, neon = 960 }, s)[1].fluid, 'helium',
        'the threshold: 95% is enough')
    eq(#plan.low({ oil = 950, helium = 950, neon = 950 }, s), 0, '... for every one')
    local tie = plan.low({ oil = 0, neon = 0, helium = 1000 }, s)
    eq(tie[1].fluid .. ' ' .. tie[2].fluid, 'neon oil', 'a tie goes by name: the same plan each time')

    local one = plan.slots(low, 1, 4)
    eq(#one, 1, 'T1: one slot')
    eq(one[1].fluid, 'helium', 'the most wanted')
    eq(one[1].planet .. '/' .. one[1].gas, '5/4', 'planet and gas from the recipe')
    eq(one[1].parallel, 4, 'every parallel')
    local four = plan.slots(low, 4, 64)
    local names = {}
    for i, sl in ipairs(four) do names[i] = sl.fluid end
    eq(table.concat(names, ' '), 'helium oil helium oil', 'different fluids, then in turn')
    eq(#plan.slots({}, 4, 4), 0, 'nothing low: no slots')
    eq(plan.slots(low, 1, 4, { helium = true })[1].fluid, 'oil',
        'a fluid another module pumps comes after the rest')
    eq(plan.slots(low, 1, 4, { helium = true, oil = true })[1].fluid, 'helium',
        '... and is still taken when all are')
    eq(plan.expected({ fluid = 'oil', parallel = 4 }, 30), 1400000 * 4 * 30, 'rate * parallel * batch')
end)

-- settings -----------------------------------------------------------------------------------
suite('settings', function()
    local settings = require('fp.settings')
    local warned = {}
    local function warn(fmt, ...) warned[#warned + 1] = fmt:format(...) end
    local s, err = settings.check({ version = 3, batch = 30, threshold = 0.95, targets = {
        { fluid = 'oil', target = 1e10, priority = 2 },
        { fluid = 'unobtainium', target = 1 },
        { fluid = 'oil', target = 5 },
        { fluid = 'lava', target = -1 },
        { fluid = 'helium', target = 1, enabled = false },
    } }, warn)
    check(s ~= nil, 'good settings: ' .. tostring(err))
    eq(s.version, 3, 'version')
    eq(#s.targets, 2, 'bad targets left out')
    eq(s.targets[1].priority, 2, 'priority kept')
    eq(s.targets[2].enabled, false, 'enabled kept')
    eq(#warned, 3, 'each left out is warned of')
    check(settings.check({ batch = 0, threshold = 0.9 }, nop) == nil, 'batch below 1 refused')
    check(settings.check({ batch = 129, threshold = 0.9 }, nop) == nil, 'batch above 128 refused')
    check(settings.check({ batch = 1.5, threshold = 0.9 }, nop) == nil, 'a fraction refused')
    check(settings.check({ batch = 30, threshold = 0 }, nop) == nil, 'threshold 0 refused')
    check(settings.check({ batch = 30, threshold = 1.5 }, nop) == nil, 'threshold above 1 refused')
    eq(settings.check({ batch = 30.0, threshold = 1 }, nop).batch, 30, 'an integral float is an integer')

    local defaults = { batch = 20, threshold = 0.9, targets = { { fluid = 'oil', target = 5 } } }
    local saved = nil
    local store = { read = function() return saved end,
        write = function(t) saved = t; return true end, encode = encode, decode = decode }
    local d, from = settings.load(store, defaults, nop)
    eq(from, 'defaults', 'nothing saved: the defaults')
    eq(d.version, 0, 'defaults are never the server\'s')
    eq(defaults.version, nil, 'config.lua\'s table is left alone')
    settings.save(store, s)
    local back, from2 = settings.load(store, defaults, nop)
    eq(from2, 'saved', 'saved ones win')
    eq(back.version, 3, 'with their version')
    eq(#back.targets, 2, 'and targets')
    saved = '{broken'
    local _, from3 = settings.load(store, defaults, warn)
    eq(from3, 'defaults', 'a broken file: the defaults')
    local none, _, lerr = settings.load(store, { batch = 0, threshold = 1 }, nop)
    check(none == nil and lerr:find('config.lua') ~= nil, 'bad defaults: an error naming config.lua')
end)

-- fakes --------------------------------------------------------------------------------------
local fake = {}

function fake.clock()
    local c = { now = 100 }
    function c.time() return c.now end
    return c
end

---A pump module: a pulse of work allowed starts one cycle of params.batch seconds (unless
---`noStart`), the module runs until then.
function fake.pump(clock, address, name, opts)
    opts = opts or {}
    local m = { address = address, params = {}, allowed = false, active = opts.active or false,
        starts = 0, endsAt = opts.endsAt or math.huge }
    function m.getName()
        if opts.nameFails then error('chunk unloaded') end
        return name
    end
    function m.setParameter(k, v)
        if opts.paramFails then error('bad parameter ' .. k) end
        m.params[k] = v
    end
    function m.setWorkAllowed(v)
        m.allowed = v
        if v and not m.active and not opts.noStart then
            m.active, m.starts = true, m.starts + 1
            m.endsAt = clock.now + (opts.length or m.params.batch)
        end
    end
    function m.isMachineActive()
        if m.active and clock.now >= m.endsAt then m.active = false end
        return m.active
    end
    function m.getParameters() return { batch = m.params.batch or 1 } end
    function m.getWorkProgress() return 200 end
    function m.getWorkMaxProgress() return 600 end
    return m
end

-- module ---------------------------------------------------------------------------------------
suite('module', function()
    local module = require('fp.module')
    local clock = fake.clock()
    local proxy = fake.pump(clock, 'aaaa1111', 'projectmodulepumpt2')
    proxy.allowed = true -- left so by the old script
    local m = module.new(proxy, module.kTiers.projectmodulepumpt2, clock.now)
    eq(proxy.allowed, false, 'work allowed taken away at once')
    eq(m.state, 'idle', 'an inactive module is idle')
    local slots = { { fluid = 'oil', planet = 4, gas = 3, parallel = 4 },
        { fluid = 'helium', planet = 5, gas = 4, parallel = 4 } }
    local c = m:start(slots, 30, 7, clock.now)
    check(c ~= nil, 'a cycle started')
    eq(proxy.params.batch, 30, 'batch')
    eq(proxy.params['recipe0.planetType'] .. '/' .. proxy.params['recipe0.gasType'], '4/3', 'slot 0')
    eq(proxy.params['recipe1.planetType'] .. '/' .. proxy.params['recipe1.gasType'], '5/4', 'slot 1')
    eq(proxy.params['recipe2.parallel'], 0, 'an empty slot gets no parallels')
    eq(proxy.params['recipe1.parallel'], 4, 'a used slot every parallel')
    eq(proxy.allowed, true, 'pulse on')
    eq(m:step(clock.now + 0.05), nil, 'nothing before the pulse is over')
    eq(proxy.allowed, true, 'still on')
    clock.now = clock.now + 0.1
    eq(m:step(clock.now), nil, 'the pulse ends: no cycle ended')
    eq(proxy.allowed, false, 'pulse off: one cycle only')
    eq(m.state, 'running', 'it runs')
    eq(m.at, c.startedAt + 30, 'next look when the batch should be over')
    clock.now = clock.now + 29.9
    local ended = m:step(clock.now)
    eq(ended and ended.result, 'done', 'done when it stops')
    eq(ended.seq, 7, 'its sequence number')
    check(ended.duration and ended.duration >= 30, 'with how long it took')
    eq(m.state, 'idle', 'idle again')
    eq(proxy.starts, 1, 'the module ran one cycle')

    -- a module that does not start: failed, then a pause before it is tried again
    clock = fake.clock()
    proxy = fake.pump(clock, 'bbbb2222', 'projectmodulepumpt1', { noStart = true })
    m = module.new(proxy, module.kTiers.projectmodulepumpt1, clock.now)
    m:start({ slots[1] }, 30, 1, clock.now)
    local result
    for _ = 1, 10 do
        clock.now = math.max(clock.now, m.at)
        result = m:step(clock.now) or result
        if m.state == 'backoff' then break end
    end
    eq(result and result.result, 'failed', 'did not start: failed')
    eq(result.message, 'did not start', '... saying so')
    eq(proxy.allowed, false, 'nothing left allowed')
    check(m.at - result.startedAt >= 30, 'a pause before the next try')
    clock.now = m.at
    m:step(clock.now)
    eq(m.state, 'idle', 'then idle')

    -- a cycle from before the agent: waited for, never reported
    clock = fake.clock()
    proxy = fake.pump(clock, 'cccc3333', 'projectmodulepumpt1', { active = true, endsAt = 105 })
    m = module.new(proxy, module.kTiers.projectmodulepumpt1, clock.now)
    eq(m.state, 'running', 'an active module runs a cycle of before')
    clock.now = 106
    eq(m:step(clock.now), nil, 'its end is not ours to report')
    eq(m.state, 'idle', 'idle then')

    -- a late cycle: warned of once, done when it stops
    clock = fake.clock()
    local warned = {}
    proxy = fake.pump(clock, 'dddd4444', 'projectmodulepumpt1', { length = 100 })
    m = module.new(proxy, module.kTiers.projectmodulepumpt1, clock.now,
        function(fmt, ...) warned[#warned + 1] = fmt:format(...) end)
    m:start({ slots[1] }, 30, 2, clock.now)
    local late
    while clock.now < 200 do
        clock.now = m.at
        late = m:step(clock.now) or late
    end
    eq(#warned, 1, 'a late cycle is warned of once')
    eq(late and late.result, 'done', 'and is done when it stops')

    -- a failing component call: failed, not thrown
    clock = fake.clock()
    proxy = fake.pump(clock, 'eeee5555', 'projectmodulepumpt1', { paramFails = true })
    m = module.new(proxy, module.kTiers.projectmodulepumpt1, clock.now)
    eq(m:start({ slots[1] }, 30, 3, clock.now), nil, 'a parameter that fails: no cycle')
    eq(m.state, 'backoff', 'a pause')
    check(m.cycle.message:find('bad parameter') ~= nil, 'the error is the message')
end)

-- pumps: the agent ---------------------------------------------------------------------------
local function fakeWorld(clock, pumpsByAddress, fluids)
    local w = { requests = {}, replies = {}, saved = nil, fluidReads = 0 }
    w.deps = {
        list = function()
            local out = {}
            for address in pairs(pumpsByAddress) do out[address] = true end
            return out
        end,
        proxy = function(address) return pumpsByAddress[address] end,
        fluids = function()
            w.fluidReads = w.fluidReads + 1
            if w.fluidsFail then error('network gone') end
            local out = {}
            for name, amount in pairs(fluids) do
                out[#out + 1] = { name = name, label = name, amount = amount, size = amount }
            end
            return out
        end,
        post = function(path, body)
            w.requests[#w.requests + 1] = { path = path, body = body }
            local r = w.replies[#w.requests]
            if r == false then return nil, 'connection refused' end
            return r or '{poll=10}'
        end,
        decode = decode,
        store = { read = function() return w.saved end,
            write = function(t) w.saved = t; return true end, encode = encode, decode = decode },
        defaults = { batch = 30, threshold = 0.95, targets = {
            { fluid = 'oil', target = 10000000000, priority = 1 },
            { fluid = 'helium', target = 10000000000, priority = 3 },
            { fluid = 'neon', target = 1000000000, priority = 1 },
        } },
        run = 'test-1',
    }
    return w
end

suite('pumps', function()
    local pumps = require('fp.pumps')
    local clock = fake.clock()
    local pa = fake.pump(clock, 'aaaa', 'projectmodulepumpt1')
    local pb = fake.pump(clock, 'bbbb', 'projectmodulepumpt1')
    local other = fake.pump(clock, 'zzzz', 'multimachine.assemblyline')
    local list = { aaaa = pa, bbbb = pb, zzzz = other }
    local w = fakeWorld(clock, list, { oil = 0, helium = 0, neon = 2000000000 })
    local p = pumps.new(w.deps, clock.now)
    check(p ~= nil, 'the agent starts on the defaults')
    local wait = p:step(clock.now, clock.time)

    eq(pa.starts + pb.starts, 2, 'both modules started')
    eq(pa.params['recipe0.gasType'] .. ' ' .. pb.params['recipe0.gasType'], '4 3',
        'different fluids: helium (priority 3) and oil')
    eq(other.allowed, false, 'a machine that is no pump module is left alone')
    eq(w.fluidReads, 1, 'one read of the network serves modules that start together')
    check(wait > 0 and wait <= 0.1, 'next wake: the end of the pulse')
    eq(#w.requests, 0, 'no poll during a pulse')

    clock.now = clock.now + wait
    wait = p:step(clock.now, clock.time)
    eq(pa.allowed or pb.allowed, false, 'pulses off')
    eq(#w.requests, 1, 'the poll after the pulse')
    local body = w.requests[1].body
    eq(w.requests[1].path, '/agent/v1/pumps/poll', 'its path')
    check(body:find('"offer":{', 1, true) ~= nil, 'the server gets the defaults to take')
    check(body:find('"settings":-1', 1, true) ~= nil, 'the first poll of a run reports no version')
    check(body:find('"recipes":[', 1, true) ~= nil, 'and the recipes')
    check(body:find('"state":"running"', 1, true) ~= nil, 'running modules in the report')
    check(body:find('"progress":200', 1, true) ~= nil, 'with their progress')
    check(body:find('{"amount":0,"fluid":"oil"}', 1, true) ~= nil, 'target stock in the report')
    check(wait > 9 and wait <= 10, 'next wake: the next poll, the cycles end later')

    -- the cycles end: results go with the next poll; a new cycle each
    w.replies[2] = false -- this poll fails: the results stay
    w.replies[3] = '{poll=10,settings={version=1,batch=20,threshold=0.9,targets={'
        .. '{fluid="neon",target=1000000000,priority=1,enabled=true}}}}'
    clock.now = clock.now + 10
    p:step(clock.now, clock.time)
    eq(#w.requests, 2, 'a poll at 10 s')
    check(w.requests[2].body:find('"recipes"', 1, true) == nil, 'recipes only until a poll got through')
    clock.now = clock.now + 20.5
    for _ = 1, 5 do
        local t = p:step(clock.now, clock.time)
        clock.now = clock.now + math.max(t, 0.1)
    end
    eq(pa.starts, 2, 'a module starts again as soon as its cycle ends')
    local poll3 = w.requests[3].body
    check(poll3:find('"result":"done"', 1, true) ~= nil, 'the done cycles reach the server')
    check(w.requests[2].body:find('"result"', 1, true) == nil, '... once they ended, not before')
    check(w.requests[4].body:find('"result"', 1, true) == nil, '... and only once')
    check(poll3:find('"expected":' .. 1400000 * 4 * 30, 1, true) ~= nil, 'with what they brought')
    eq(p.settings.version, 1, 'the server\'s settings taken')
    check(w.saved and w.saved:find('neon') ~= nil, 'and saved')
    eq(#p.settings.targets, 1, 'its targets')

    -- the server's settings survive a restart; a vanished module is noted
    local w2 = fakeWorld(clock, { aaaa = pa }, { neon = 0 })
    w2.saved = w.saved
    local p2 = pumps.new(w2.deps, clock.now)
    eq(p2.settings.version, 1, 'saved settings at the start: no server needed')
    -- the first reply of a run is taken even at the same version: the saved copy may be short
    w2.replies[1] = '{poll=10,settings={version=1,batch=40,threshold=0.9,targets={'
        .. '{fluid="neon",target=1000000000,priority=1,enabled=true}}}}'
    clock.now = clock.now + 100
    p2:step(clock.now, clock.time)
    eq(pa.params['recipe0.gasType'], 6, 'neon (planet 5, gas 6) by the saved targets')
    clock.now = clock.now + 1 -- past the pulse: the poll waits for it
    p2:step(clock.now, clock.time)
    eq(p2.settings.batch, 40, 'the first reply of a run is taken at the same version')
    check(w2.requests[1] == nil or w2.requests[1].body:find('"offer"', 1, true) == nil,
        'settings of the server are not offered back')

    -- a server without settings asks for the agent's, whatever their version
    w2.replies[#w2.requests + 1] = '{poll=10,offer=true}'
    clock.now = clock.now + 20
    p2:step(clock.now, clock.time)
    clock.now = clock.now + 20
    p2:step(clock.now, clock.time)
    check(w2.requests[#w2.requests].body:find('"offer":{', 1, true) ~= nil,
        'asked for: offered, version 1 and all')

    -- nothing to pump: idle, looked at again later, no busy loop
    local clock3 = fake.clock()
    local pc = fake.pump(clock3, 'cccc', 'projectmodulepumpt1')
    local w3 = fakeWorld(clock3, { cccc = pc }, { oil = 1e11, helium = 1e11, neon = 1e11 })
    local p3 = pumps.new(w3.deps, clock3.now)
    local t3 = p3:step(clock3.now, clock3.time)
    eq(pc.starts, 0, 'everything full: no cycle')
    check(t3 >= 9.9, 'and a long wait, not a busy loop: ' .. t3)

    -- the network cannot be read: no cycle, no crash
    w3.fluidsFail = true
    clock3.now = clock3.now + 30
    p3.stock = nil
    p3:step(clock3.now, clock3.time)
    eq(pc.starts, 0, 'no stock: no cycle')

    -- a module that cannot even say its name is asked again later
    local clock4 = fake.clock()
    local shy = fake.pump(clock4, 'dddd', 'projectmodulepumpt1', { nameFails = true })
    local w4 = fakeWorld(clock4, { dddd = shy }, { oil = 0 })
    local p4 = pumps.new(w4.deps, clock4.now)
    p4:step(clock4.now, clock4.time)
    check(p4.modules.dddd == nil and not p4.ignored.dddd, 'not taken, not ignored for good')

    -- a module found: its parameters go to the log, as the game gives them
    local clock6 = fake.clock()
    local w6 = fakeWorld(clock6, { ffff = fake.pump(clock6, 'ffff', 'projectmodulepumpt1') }, {})
    local told6 = {}
    w6.deps.info = function(fmt, ...) told6[#told6 + 1] = fmt:format(...) end
    pumps.new(w6.deps, clock6.now):step(clock6.now, clock6.time)
    local found6 = false
    for _, line in ipairs(told6) do
        found6 = found6 or line:find('parameters: {"batch":', 1, true) ~= nil
    end
    check(found6, 'a found module\'s parameters are logged')

    -- a module that keeps failing is warned of once, and again when it works
    local clock5 = fake.clock()
    local dead = fake.pump(clock5, 'eeee', 'projectmodulepumpt1', { noStart = true })
    local w5 = fakeWorld(clock5, { eeee = dead }, { oil = 0 })
    local said = {}
    w5.deps.warn = function(fmt, ...) said[#said + 1] = fmt:format(...) end
    w5.deps.info = function(fmt, ...) said[#said + 1] = fmt:format(...) end
    local p5 = pumps.new(w5.deps, clock5.now)
    for _ = 1, 400 do
        clock5.now = clock5.now + math.max(p5:step(clock5.now, clock5.time), 0.1)
    end
    local failures5 = 0
    for _, line in ipairs(said) do
        if line:find('failed', 1, true) then failures5 = failures5 + 1 end
    end
    eq(failures5, 1, 'the same failure is told once')
    check(#p5.ended + #w5.requests > 0, 'its cycles still go to the server')

    -- stop: nothing left allowed
    p:stop()
    eq(pa.allowed or pb.allowed, false, 'stop leaves nothing allowed to work')
end)

for _, s in ipairs(suites) do
    local ok, err = xpcall(s.fn, debug.traceback)
    if not ok then
        failures = failures + 1
        print(('FAIL suite %s: %s'):format(s.name, err))
    end
end
print(('%d checks, %d failed'):format(total, failures))
os.exit(failures == 0 and 0 or 1)
