---Local tests for the pure logic and the runner, with fake components. Not installed in game.
---  lua tests/run.lua   (from the repository root)
package.path = './?.lua;' .. package.path

local config = require('qft.config')
local log = require('qft.log')
local plan = require('qft.plan')
local resource = require('qft.resource')
local runner = require('qft.runner')

log.setLevel(log.Level.Err + 1) -- keep test output clean

local failures = 0
local function check(cond, what)
    if not cond then
        failures = failures + 1
        print('FAIL ' .. what)
    end
end

local function recipes()
    local r = {
        rubber = { outputs = {
            { fluid = 'A', circuit = 1, target = 100 },
            { fluid = 'U', circuit = 2 }, -- not tracked
            { fluid = 'B', circuit = 3, target = 100, priority = 2 },
        } },
        untracked = { outputs = { { fluid = 'X', circuit = 1 } } },
    }
    assert(#config.normalize(r) == 0)
    return r
end

local function fluids(list)
    local out = {}
    for label, amount in pairs(list) do
        out[#out + 1] = { label = label, name = label:lower(), amount = amount }
    end
    return resource.snapshot(nil, out)
end

-- resource.fromSpec
do
    local r = resource.fromSpec({ item = 'Glue', damage = 3 })
    check(r and r.kind == 'item' and r.label == 'Glue' and r.damage == 3, 'item spec')
    check(resource.fromSpec({ fluid = 'Water' }).kind == 'fluid', 'fluid spec')
    check(resource.fromSpec({ item = 'a', fluid = 'b' }) == nil, 'both kinds refused')
    check(resource.fromSpec({ circuit = 1 }) == nil, 'no label refused')
    check(resource.fromSpec({ fluid = 'W', damage = 1 }) == nil, 'damage on a fluid refused')
    check(resource.fromSpec({ item = 'W', damage = 1.5 }) == nil, 'fractional damage refused')
    local problems = config.normalize({ r = { outputs = { { circuit = 1 } } } })
    check(#problems == 1 and problems[1]:find('outputs%[1%]'), 'normalize reports the entry')
end

-- resource.amount
do
    local snap = resource.snapshot({
        { label = 'Dust', name = 'bw:dust', damage = 11, size = 5 },
        { label = 'Pane', name = 'minecraft:glass_pane', damage = 0, size = 1 },
        { label = 'Pane', name = 'gregtech:gt.metaitem.01', damage = 17890, size = 2 },
        { label = 'Tool', name = 'mod:tool', damage = 0, size = 1, hasTag = true },
        { label = 'Tool', name = 'mod:tool', damage = 0, size = 1, hasTag = true },
        { label = 'Drop', name = 'ae2fc:fluid_drop', damage = 0, size = 9, hasTag = true },
    }, {
        { label = 'A', name = 'a', amount = 5 }, { label = 'A', name = 'a', amount = 7 },
        { label = 'D', name = 'd1', amount = 1 }, { label = 'D', name = 'd2', amount = 1 },
    })
    local function amount(spec) return resource.amount(snap, resource.fromSpec(spec)) end

    check(amount({ item = 'Dust' }) == 5, 'item amount is its size')
    check(amount({ fluid = 'A' }) == 12, 'same fluid name sums')
    local n, _, absent = amount({ item = 'Nothing' })
    check(n == 0 and absent, 'absent is 0 and flagged')
    check(amount({ fluid = 'Dust' }) == 0, 'kinds do not mix: an item label is no fluid')
    local a, err = amount({ item = 'Pane' })
    check(a == nil and err:find('17890'), 'different items under one label are ambiguous')
    check(amount({ item = 'Pane', name = 'gregtech:gt.metaitem.01' }) == 2, 'name resolves it')
    check(amount({ item = 'Pane', damage = 0 }) == 1, 'damage resolves it')
    a, err = amount({ item = 'Tool' })
    check(a == nil and err:find('NBT'), 'NBT-only duplicates are ambiguous')
    check(amount({ item = 'Drop' }) == 0, 'AE2FC fluid drops are ignored')
    a = amount({ fluid = 'D' })
    check(a == nil, 'different fluid names are ambiguous')
    a, err = resource.amount(resource.snapshot(nil, {}), resource.fromSpec({ item = 'Dust' }))
    check(a == nil and err:find('not read'), 'unread kind is an error, not 0')
end

-- plan.evaluate + choose: hysteresis
do
    local function want(a, b, current)
        local states = plan.evaluate(recipes(), fluids({ A = a, B = b }))
        return plan.choose(states, current), states
    end
    check(want(90, 90, nil) == nil, 'above start ratio: does not start')
    check(want(70, 90, nil) == 'rubber', 'below start ratio: starts')
    check(want(90, 90, 'rubber') == 'rubber', 'running: keeps going below target')
    check(want(100, 100, 'rubber') == nil, 'running: stops at target')
    check(want(100, 100, nil) == nil, 'untracked recipe never starts')

    local _, states = want(50, 50, nil)
    -- A: 1 - 0.5 = 0.5; B: 1 - 0.25 = 0.75 -> B is the focus
    check(states.rubber.focus == 3, 'focus is the most urgent output')
    check(math.abs(states.rubber.urgency - 0.75) < 1e-9, 'urgency uses priority exponent')

    local _, _, missing = plan.evaluate(recipes(), fluids({}))
    check(#missing == 2, 'missing tracked labels are reported')

    local broken = plan.evaluate(recipes(), resource.snapshot(nil, {
        { label = 'A', name = 'a1', amount = 0 }, { label = 'A', name = 'a2', amount = 0 },
    }))
    check(plan.choose(broken, 'rubber') == nil, 'ambiguous label stops the recipe')
end

-- mixed item + fluid recipe
do
    local r = { glue = { outputs = {
        { item = 'Adhesive', circuit = 1, target = 4096, priority = 3 },
        { fluid = 'Indalloy', circuit = 2, target = 1000 },
    } } }
    assert(#config.normalize(r) == 0)
    local snap = resource.snapshot(
        { { label = 'Adhesive', name = 'gt:adh', damage = 0, size = 1024 } },
        { { label = 'Indalloy', name = 'indalloy', amount = 1000 } })
    local states = plan.evaluate(r, snap)
    check(states.glue.low and not states.glue.done, 'item output below target starts the recipe')
    check(states.glue.focus == 1, 'the item output can be the focus')
    check(plan.choose(states, nil) == 'glue', 'mixed recipe is chosen')
end

-- runner with fakes
local function fakeMachine(active)
    local m = { allowed = true, active = active or false, calls = 0 }
    m.setWorkAllowed = function(v) m.allowed = v; m.calls = m.calls + 1 end
    m.circuit = -1
    m.setCircuitConfiguration = function(v) m.circuit = v; return true end
    m.isMachineActive = function() return m.active end
    return m
end

local function fakeSetup()
    local c = {
        controller = fakeMachine(true),
        plasma = { fermium = { hatch = fakeMachine() }, neptunium = { hatch = fakeMachine() } },
        recipes = {
            rubber = { bus = fakeMachine(), hatch = fakeMachine() },
            other = { bus = fakeMachine() },
        },
    }
    return c
end

local R = { recipe = 'rubber', fermium = false }
local O = { recipe = 'other', fermium = false }

do
    local c = fakeSetup()
    local r = runner.new(c, 0)
    check(r.phase == 'stopping' and c.controller.allowed == false, 'starts by stopping')

    runner.step(r, R, 1)
    check(r.phase == 'stopping', 'waits while the machine finishes its cycle')
    check(c.recipes.rubber.bus.allowed == true, 'inputs untouched while active')

    c.controller.active = false
    runner.step(r, R, 2)
    check(c.plasma.fermium.hatch.allowed == false and c.recipes.other.bus.allowed == false,
        'idle switches every input off')
    check(r.phase == 'running' and r.running.recipe == 'rubber', 'idle -> running in the same tick')
    check(c.recipes.rubber.bus.allowed and c.recipes.rubber.hatch.allowed
        and c.controller.allowed, 'recipe inputs and controller enabled')

    c.controller.active = true
    local calls = c.recipes.rubber.bus.calls
    runner.step(r, R, 3)
    check(c.recipes.rubber.bus.calls == calls, 'no switching while the decision holds')

    runner.step(r, O, 4)
    check(r.phase == 'stopping' and c.controller.allowed == false, 'new decision: stop first')
    check(c.recipes.rubber.bus.allowed == true, 'old inputs stay on until the cycle ends')

    c.controller.active = false
    runner.step(r, O, 5)
    check(r.running.recipe == 'other' and c.recipes.rubber.bus.allowed == false
        and c.recipes.other.bus.allowed == true, 'switched after the cycle ended')

    runner.step(r, nil, 6)
    check(r.phase == 'idle' and c.controller.allowed == false, 'nothing wanted: QFT stops')
end

do
    local c = fakeSetup()
    c.controller.isMachineActive = function() error('chunk unloaded') end
    local r = runner.new(c, 0)
    runner.step(r, R, 1)
    check(r.phase == 'stopping', 'failing controller: stays stopping, does not crash')

    c = fakeSetup()
    c.controller.active = false
    c.recipes.rubber.hatch.setWorkAllowed = function() error('gone') end
    r = runner.new(c, 0)
    runner.step(r, R, 1)
    check(r.phase == 'stopping' and c.controller.allowed == false,
        'failed start falls back to stopping')
end

-- plasma
do
    local recipe = { tier = 1, catalysts = 4 }
    check(plan.plasmaRate(2, 4) == 16, 'rate = 4 * tier * sqrt(parallels)')
    local opts = { plasmaStartSeconds = 100, plasmaKeepSeconds = 10 }
    local function use(stock, running, cycle, shielding)
        return (plan.usePlasma({ recipe = recipe, shieldingTier = shielding or 1, stock = stock,
            running = running, cycleSeconds = cycle, opts = opts }))
    end
    -- rate = 4 * 1 * 2 = 8 L/s
    check(use(800, false) == true and use(799, false) == false, 'start needs startSeconds of stock')
    check(use(480, true, 50) == true, 'running keeps while cycle + margin is covered')
    check(use(479, true, 50) == false, 'running stops below cycle + margin')
    check(use(1e9, false, nil, 1) == true, 'equal tiers: focusing works')
    recipe.tier = 2
    check(use(1e9, false, nil, 1) == false, 'shielding below recipe tier: never')
end

do
    local c = fakeSetup()
    c.controller.active = false
    local r = runner.new(c, 0)
    runner.step(r, { recipe = 'rubber', fermium = true }, 1)
    check(c.plasma.fermium.hatch.allowed == true and c.plasma.neptunium.hatch.allowed == false,
        'fermium decision enables only the fermium hatch')

    c.controller.active = true
    c.controller.getWorkMaxProgress = function() return 1200 end
    runner.step(r, { recipe = 'rubber', fermium = true }, 2)
    check(r.cycleSeconds == 60, 'cycle length read from the controller in seconds')

    runner.step(r, R, 3)
    check(r.phase == 'stopping' and c.plasma.fermium.hatch.allowed == true,
        'dropping fermium waits for the cycle to end')
    c.controller.active = false
    runner.step(r, R, 4)
    check(c.plasma.fermium.hatch.allowed == false and r.running.recipe == 'rubber'
        and r.running.fermium == false, 'restarted without fermium after the cycle')
end

-- Neptunium: chances ported from the source, checked against the wiki Summary table
do
    local function pct(t) local o = {} for i, c in ipairs(t) do o[i] = c / 100 end return o end
    local c = pct(plan.chances(5, 0, 4, false))
    check(c[4] == 60 and c[1] == 10, 'neptunium d=0, 5 outputs: 60 / 10')
    c = pct(plan.chances(5, 1, 4, false))
    check(c[4] == 80 and c[1] == 5, 'neptunium d=1: 80 / 5')
    c = pct(plan.chances(5, 2, 4, false))
    check(c[4] == 100 and c[1] == 0, 'neptunium d=2: 100 / 0')
    c = pct(plan.chances(5, 0, nil, true))
    check(c[1] == 40, 'fermium d=0: 40')
    c = pct(plan.chances(5, 0, 4, true))
    check(c[4] == 70 and c[1] == 32.5, 'both d=0: 70 / 32.5')
    c = pct(plan.chances(4, -1, 1, true))
    check(c[1] == 25 and c[2] == 25, 'd<0: no effect')
end

do
    local function focus(weights, current, delta)
        return (plan.chooseFocus({ n = 4, delta = delta or 0, weights = weights,
            fermium = false, current = current }))
    end
    check(focus({ 1, 1, 1, 1 }) == nil, 'equal need: Neptunium does not pay')
    check(focus({ 0.9, 0, 0.1, 0 }) == 1, 'focus on the most needed output')
    check(focus({ 0, 0, 0, 0 }) == nil, 'nothing needed: no focus')
    check(focus({ 1, 1, 1, 1 }, nil, -1) == nil, 'd<0: never')
    -- current focus 1 holds while another output is only slightly better
    check(focus({ 0.50, 0, 0.52, 0 }, 1) == 1, 'focus hysteresis: holds')
    check(focus({ 0.30, 0, 0.60, 0 }, 1) == 3, 'focus moves when clearly beaten')
    check(focus({ 0, 0, 0.6, 0 }, 3) == 3, 'only one needed: keeps focus')
    -- the current focus became useless but another output needs it: move, do not switch off
    check(focus({ 0.7, 0, 0.1, 0 }, 3) == 1, 'useless focus moves instead of switching off')
end

do
    local c = fakeSetup()
    c.controller.active = false
    local r = runner.new(c, 0)
    local N1 = { recipe = 'rubber', fermium = false, neptunium = 1 }
    local N3 = { recipe = 'rubber', fermium = false, neptunium = 3 }
    -- the circuit must be set before the hatch: check the order inside the hatch call
    local circuitAtEnable
    local hatch = c.plasma.neptunium.hatch
    local set = hatch.setWorkAllowed
    hatch.setWorkAllowed = function(v)
        if v then circuitAtEnable = c.recipes.rubber.bus.circuit end
        set(v)
    end
    runner.step(r, N1, 1)
    check(circuitAtEnable == 1 and hatch.allowed, 'circuit set before the neptunium hatch')

    c.controller.active = true
    runner.step(r, N3, 2)
    check(r.phase == 'running' and c.recipes.rubber.bus.circuit == 3,
        'focus moves on the fly, no stop')

    runner.step(r, R, 3)
    check(r.phase == 'stopping' and c.recipes.rubber.bus.circuit == 3 and hatch.allowed,
        'neptunium off waits for the cycle, circuit kept while the hatch is on')
    c.controller.active = false
    local circuitAtDisable
    hatch.setWorkAllowed = function(v)
        if not v then circuitAtDisable = c.recipes.rubber.bus.circuit end
        set(v)
    end
    runner.step(r, R, 4)
    check(circuitAtDisable == 3, 'hatch off before the circuit is reset')
    check(c.recipes.rubber.bus.circuit == -1 and r.running.neptunium == nil,
        'restarted without neptunium and without circuit')

    c = fakeSetup()
    c.controller.active = false
    c.recipes.rubber.bus.setCircuitConfiguration = function() return nil, 'no slot' end
    r = runner.new(c, 0)
    runner.step(r, N1, 1)
    check(r.phase == 'stopping' and c.plasma.neptunium.hatch.allowed == false,
        'refused circuit: never starts neptunium')
end

print(failures == 0 and 'all tests passed' or (failures .. ' test(s) failed'))
os.exit(failures == 0 and 0 or 1)
