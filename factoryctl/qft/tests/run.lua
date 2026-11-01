---Local tests for the pure logic and the runner, with fake components. Not installed in game.
---  lua tests/run.lua   (from the repository root)
package.path = './?.lua;../agent/?.lua;' .. package.path

local bind = require('qft.bind')
local config = require('qft.config')
local log = require('qft.log')
local plan = require('qft.plan')
local resource = require('qft.resource')
local runner = require('qft.runner')
local slots = require('qft.slots')

log.setLevel(log.Level.Err + 1) -- keep test output clean

local failures = 0
local function check(cond, what)
    if not cond then
        failures = failures + 1
        print('FAIL ' .. what)
    end
end

-- What config.checkValues needs of the components.
local function components()
    return { shieldingTier = 1, bus = bind.manual { type = 'me_inputbus', prompt = 'bus' },
        hatch = bind.manual { type = 'me_inputhatch', prompt = 'hatch' },
        plasma = { hatch = bind.manual { type = 'me_inputhatch', prompt = 'plasma' },
            neptunium = 'Neptunium Plasma', fermium = 'Fermium Plasma' } }
end

local function recipes()
    local r = {
        rubber = { trigger = 'deficit', outputs = {
            { fluid = 'A', circuit = 1, target = 100 },
            { fluid = 'U', circuit = 2 }, -- not tracked
            { fluid = 'B', circuit = 3, target = 100, priority = 2 },
        } },
        untracked = { trigger = 'deficit', outputs = { { fluid = 'X', circuit = 1 } } },
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
    -- A: 1 - 0.5 = 0.5; B: 1 - 0.25 = 0.75
    check(states.rubber.weights[1] == 0.5 and states.rubber.weights[3] == 0.75
        and states.rubber.weights[2] == nil, 'deficit weights are urgencies of tracked outputs')
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
    check(states.glue.weights[1] > 0 and states.glue.weights[2] == 0,
        'the item output weighs for the focus')
    check(plan.choose(states, nil) == 'glue', 'mixed recipe is chosen')
end

-- config: each trigger has its own fields
do
    local function problems(recipe, keepInputs)
        recipe.tier, recipe.catalysts = recipe.tier or 1, recipe.catalysts or 1
        if not keepInputs and recipe.inputs == nil and recipe.trigger ~= 'surplus' then
            recipe.inputs = { { fluid = 'Water' } }
        end
        local list = config.checkValues(components(), { r = recipe })
        return table.concat(list, '\n')
    end
    local out = function(extra)
        local o = { fluid = 'A', circuit = 1 }
        for k, v in pairs(extra or {}) do o[k] = v end
        return { o }
    end
    local input = { { item = 'Dust', amount = 32 } }
    check(problems({ trigger = 'deficit', outputs = out({ target = 1 }) }) == '', 'valid deficit')
    check(problems({ trigger = 'surplus', inputs = input, outputs = out({ weight = 2 }) }) == '',
        'valid surplus')
    check(problems({ outputs = out({ target = 1 }) }):find('trigger'), 'trigger is required')
    check(problems({ trigger = 'deficit', outputs = out() }):find('needs a target'),
        'deficit without a target refused')
    check(problems({ trigger = 'deficit', outputs = out({ target = 1, weight = 1 }) })
        :find('weight is for surplus'), 'weight on deficit refused')
    check(problems({ trigger = 'surplus', inputs = input, outputs = out({ target = 1 }) })
        :find('use weight'), 'target on surplus refused')
    check(problems({ trigger = 'surplus', outputs = out({ weight = 1 }) }):find('inputs must be'),
        'surplus without inputs refused')
    check(problems({ trigger = 'deficit', outputs = out({ target = 1 }) }, true):find('inputs must be'),
        'deficit without inputs refused: the bus is set from them')
    check(problems({ trigger = 'deficit', inputs = { { fluid = 'Water' } },
        outputs = out({ target = 1 }) }) == '', 'a deficit input needs no amount')
    local many = {}
    for i = 1, 17 do many[i] = { item = 'Dust ' .. i } end
    check(problems({ trigger = 'deficit', inputs = many, outputs = out({ target = 1 }) })
        :find('17 item inputs'), 'more items than bus slots refused')
    check(table.concat(config.checkValues({ shieldingTier = 1 }, {}), ' '):find('bus must be'),
        'the bus and the hatch are required components')
    check(table.concat(config.checkValues({ shieldingTier = 1, plasma = {
        neptunium = { hatch = bind.manual { type = 'me_inputhatch', prompt = 'n' } } } }, {}), ' ')
        :find('plasma: hatch must be'), 'old per-plasma hatches refused')
    local named = components()
    named.server, named.name = 'http://127.0.0.1:8090', 'qft-main'
    check(#config.checkValues(named, {}) == 0, 'server and name are fine')
    named.name = ''
    check(table.concat(config.checkValues(named, {}), ' '):find('name must be'), 'an empty name refused')
    local old = components()
    old.recipes = {}
    check(table.concat(config.checkValues(old, {}), ' '):find('recipes are gone'),
        'per-recipe components are refused with a hint')
    check(problems({ trigger = 'surplus', inputs = { item = 'Dust', amount = 32 },
        outputs = out() }):find('must be a list'), 'old single-input format refused')
    check(problems({ trigger = 'surplus', inputs = { { item = 'Dust' } }, outputs = out() })
        :find('amount'), 'input without amount refused')
    check(problems({ trigger = 'surplus', inputs = input, outputs = out({ weight = -1 }) })
        :find('weight must be'), 'negative weight refused')
    local r = { r = { inputs = { { circuit = 1, amount = 1 } }, outputs = {} } }
    local np = config.normalize(r)
    check(#np == 1 and np[1]:find('inputs%[1%]'), 'normalize reports a bad input')
end

-- two triggers: deficit maintains products, surplus processes raw materials
local function twoModes()
    local r = {
        glue = { trigger = 'deficit', catalysts = 1, outputs = {
            { fluid = 'Glue', circuit = 1, target = 100 },
        } },
        plat = { trigger = 'surplus', catalysts = 2, inputs = {
            { item = 'Dust', amount = 10 }, -- 20 per cycle
            { fluid = 'Acid', amount = 50 }, -- 100 per cycle
        }, outputs = {
            { fluid = 'Pt', circuit = 1, weight = 3 },
            { fluid = 'Pd', circuit = 2 },
        } },
        ore = { trigger = 'surplus', catalysts = 1, inputs = { { item = 'Ore', amount = 10 } },
            outputs = { { fluid = 'Os', circuit = 1, weight = 1 } } },
    }
    assert(#config.normalize(r) == 0)
    return r
end

local function states(glue, dust, acid, ore)
    local source = resource.snapshot({
        { label = 'Dust', name = 'dust', damage = 0, size = dust },
        { label = 'Ore', name = 'ore', damage = 0, size = ore or 0 },
    }, { { label = 'Acid', name = 'acid', amount = acid } })
    return (plan.evaluate(twoModes(), fluids({ Glue = glue }), source))
end

do
    local st = states(100, 60, 1000)
    check(st.plat.cycles == 3, 'cycles: the scarcest input decides (dust 60/20 = 3, acid 10)')
    check(st.plat.weights[1] == 3 and st.plat.weights[2] == 0, 'surplus weights, absent = 0')
    check(plan.choose(st, nil) == 'plat', 'no product short: surplus runs')
    check(plan.choose(states(100, 39, 1000), nil) == nil, 'below start cycles: does not start')
    check(plan.choose(states(100, 39, 1000), 'plat') == 'plat', 'running: keeps above stop cycles')
    check(plan.choose(states(100, 19, 1000), 'plat') == nil, 'below one cycle: stops')
    check(plan.choose(states(100, 60, 50), nil) == nil, 'any scarce input blocks the start')

    check(plan.choose(states(70, 60, 1000), nil) == 'glue', 'deficit comes first')
    check(plan.choose(states(70, 60, 1000), 'plat') == 'glue', 'surplus yields to a deficit')
    check(plan.choose(states(90, 60, 1000), 'glue') == 'glue', 'deficit keeps going until done')
    check(plan.choose(states(100, 60, 1000), 'glue') == 'plat', 'deficit done: surplus takes over')

    -- plat 3 cycles, ore 5 then 6: switching needs 2x the running recipe's cycles
    check(plan.choose(states(100, 60, 1000, 50), 'plat') == 'plat', 'surplus switch: hysteresis')
    check(plan.choose(states(100, 60, 1000, 60), 'plat') == 'ore', 'surplus switch: 2x more stock')
    check(plan.choose(states(100, 60, 1000, 60), nil) == 'ore', 'idle: most cycles in stock wins')

    local deficitOnly, surplusOnly = { mode = 'deficit' }, { mode = 'surplus' }
    check(plan.choose(states(100, 60, 1000), nil, deficitOnly) == nil, 'deficit mode: no surplus')
    check(plan.choose(states(70, 60, 1000), nil, deficitOnly) == 'glue', 'deficit mode: deficit runs')
    check(plan.choose(states(70, 60, 1000), nil, surplusOnly) == 'plat', 'surplus mode: no deficit')
    check(plan.choose(states(70, 60, 1000), 'plat', surplusOnly) == 'plat',
        'surplus mode: running surplus does not yield to a deficit')
    check(plan.choose(states(70, 60, 1000), 'glue', surplusOnly) == 'plat',
        'surplus mode: a running deficit is not kept')
    check(plan.choose(states(100, 60, 1000), 'plat', deficitOnly) == nil,
        'deficit mode: a running surplus is not kept')

    local _, problems = plan.evaluate(twoModes(), fluids({ Glue = 100 }), nil)
    check(#problems == 2 and problems[1]:find('not read'), 'unread source network is an error')

    local r = { glue = { trigger = 'deficit', catalysts = 1,
        inputs = { { item = 'Dust', amount = 10 } },
        outputs = { { fluid = 'Glue', circuit = 1, target = 100 } } } }
    assert(#config.normalize(r) == 0)
    local function deficit(dust, current)
        local source = resource.snapshot({ { label = 'Dust', name = 'd', damage = 0, size = dust } })
        return plan.choose((plan.evaluate(r, fluids({ Glue = 10 }), source)), current)
    end
    check(deficit(10) == 'glue', 'deficit with inputs: one cycle is enough to start')
    check(deficit(9) == nil and deficit(9, 'glue') == nil, 'deficit without raw materials: never runs')
end

-- catalysts = 0: the recipe stays in the config but never runs
do
    local r = twoModes()
    r.ore.catalysts = 0
    r.glue.catalysts = 0
    local source = resource.snapshot({
        { label = 'Dust', name = 'dust', damage = 0, size = 60 },
        { label = 'Ore', name = 'ore', damage = 0, size = 1000 },
    }, { { label = 'Acid', name = 'acid', amount = 1000 } })
    local st, problems = plan.evaluate(r, fluids({ Glue = 0 }), source)
    check(#problems == 0, 'no catalysts is not a problem')
    check(st.ore.disabled and not st.ore.low and st.ore.done and st.ore.cycles == nil,
        'no catalysts: disabled, no division by zero')
    check(plan.choose(st, nil) == 'plat', 'disabled recipes do not block the others')
    check(plan.choose(st, 'ore') == 'plat' and plan.choose(st, 'glue') == 'plat',
        'a running disabled recipe is stopped')

    local function problemsOf(catalysts)
        return config.checkValues(components(), { r = { tier = 1, catalysts = catalysts,
            trigger = 'deficit', inputs = { { fluid = 'W' } },
            outputs = { { fluid = 'A', circuit = 1, target = 1 } } } })
    end
    check(#problemsOf(0) == 0, 'config: catalysts = 0 allowed')
    check(#problemsOf(-1) == 1, 'config: negative catalysts refused')
end

-- config.build: recipes from the catalog and the player's choice
do
    local catalog = {
        plat = { name = 'Platinum Dust', tier = 1, seconds = 20,
            inputs = { { item = 'Powder', name = 'bw:dust', damage = 47, amount = 32 },
                { fluid = 'Oxygen', name = 'oxygen', amount = 100 } },
            outputs = { { item = 'Platinum Dust', name = 'gt:meta', damage = 2085, amount = 64,
                molten = { fluid = 'Molten Platinum', name = 'molten.platinum', amount = 9216 } },
                { item = 'Inert Dust', name = 'bw:dust', damage = 10113, amount = 1 } } },
    }
    local r, problems = config.build(catalog, { plat = { trigger = 'surplus', catalysts = 2,
        outputs = { [1] = { weight = 3 } } } }, false)
    check(#problems == 0, 'build: no problems')
    local p = r.plat
    check(p.name == 'Platinum Dust' and p.tier == 1 and p.catalysts == 2, 'build: name, tier, catalysts')
    check(p.inputs[1].item == 'Powder' and p.inputs[1].name == 'bw:dust' and p.inputs[1].damage == 47
        and p.inputs[1].amount == 32 and p.inputs[2].fluid == 'Oxygen', 'build: inputs with names')
    check(p.outputs[1].item == 'Platinum Dust' and p.outputs[1].circuit == 1 and p.outputs[1].weight == 3,
        'build: outputs, circuit = place, settings by circuit')
    r = config.build(catalog, { plat = { trigger = 'surplus', catalysts = 1 } }, true)
    check(r.plat.outputs[1].fluid == 'Molten Platinum' and r.plat.outputs[1].name == 'molten.platinum',
        'fluid mode: a dust with a molten form counts as the fluid')
    check(r.plat.outputs[2].item == 'Inert Dust', 'fluid mode: one without stays an item')
    r = config.build(catalog, { plat = { trigger = 'deficit', catalysts = 1, countInputs = false,
        name = 'Platinum Group' } }, false)
    check(r.plat.name == 'Platinum Group' and r.plat.inputs[1].amount == nil,
        'build: own name, inputs not counted')
    local _, bad = config.build(catalog, { nope = {}, plat = { outputs = { [7] = {} } } }, false)
    check(#bad == 2 and table.concat(bad, ' '):find('no such recipe') and table.concat(bad, ' '):find('2 outputs'),
        'build: an unknown id and a circuit past the outputs are problems')
    assert(#config.normalize(config.build(catalog, { plat = { trigger = 'surplus', catalysts = 1 } }, false)) == 0)
end

-- resource.amount by internal name: the game's label need not match the catalog's
do
    local snap = resource.snapshot({ { label = 'Platinum Dust', name = 'gt:meta', damage = 2085, size = 7 } },
        { { label = 'Molten Styrene-Butadiene Rubber', name = 'molten.sbr', amount = 500 } })
    check(resource.amount(snap, { kind = 'fluid', label = 'Molten Styrene-Butadiene Rubber (SBR)',
        name = 'molten.sbr' }) == 500, 'a fluid found by name under another label')
    check(resource.amount(snap, { kind = 'item', label = 'Platinum', name = 'gt:meta', damage = 2085 }) == 7,
        'an item found by name and damage')
    local zero, _, absent = resource.amount(snap, { kind = 'item', label = 'Platinum Dust',
        name = 'gt:meta', damage = 1 })
    check(zero == 0 and absent, 'another damage: absent')
end

-- bind: a slot may take any of several component types
do
    local net = bind.manual { type = { 'fluid_interface', 'me_controller' }, prompt = 'net' }
    check(bind.accepts(net, 'me_controller') and bind.accepts(net, 'fluid_interface')
        and not bind.accepts(net, 'me_interface'), 'several types: any of them')
    local one = bind.manual { type = 'gt_machine', prompt = 'qft' }
    check(bind.accepts(one, 'gt_machine') and #bind.types(one) == 1, 'one type as before')
    check(not pcall(bind.manual, { type = {}, prompt = 'x' }), 'an empty type list is refused')
end

-- resource.identify and slots: what a stocking bus or hatch slot takes
do
    local source = resource.snapshot({
        { label = 'Carbon Dust', name = 'gregtech:gt.metaitem.01', damage = 2010, size = 5 },
        { label = 'Pane', name = 'minecraft:glass_pane', damage = 0, size = 1 },
        { label = 'Pane', name = 'gregtech:gt.metaitem.01', damage = 17890, size = 2 },
    }, { { label = 'Oxygen', name = 'oxygen', amount = 1000 } })
    local d = resource.identify(source, { kind = 'item', label = 'Carbon Dust' })
    check(d and d.name == 'gregtech:gt.metaitem.01' and d.damage == 2010, 'item found by label')
    d = resource.identify(source, { kind = 'fluid', label = 'Oxygen' })
    check(d and d.name == 'oxygen' and d.damage == nil, 'fluid found by label')
    local _, err = resource.identify(source, { kind = 'item', label = 'Pane' })
    check(err and err:find('add name/damage'), 'an ambiguous label is an error')
    _, err = resource.identify(source, { kind = 'item', label = 'Missing Dust' })
    check(err and err:find('not in the network'), 'absent: cannot set the slot')
    d = resource.identify(source, { kind = 'item', label = 'Missing Dust', name = 'x:y', damage = 3 })
    check(d and d.name == 'x:y' and d.damage == 3, 'named fully in the config: no lookup')
    d = resource.identify(source, { kind = 'fluid', label = 'Helium', name = 'helium' })
    check(d and d.name == 'helium', 'a fluid with its name: no lookup')

    local r = { inputs = { { fluid = 'Oxygen' }, { item = 'Carbon Dust', amount = 1 } }, outputs = {} }
    assert(#config.normalize({ r = r }) == 0)
    local set = slots.forRecipe(r, source)
    check(set and #set.items == 1 and #set.fluids == 1 and set.fluids[1].name == 'oxygen',
        'a recipe splits into bus items and hatch fluids')
    r.inputs[3] = { item = 'Missing Dust' }
    assert(#config.normalize({ r = r }) == 0)
    local none, serr = slots.forRecipe(r, source)
    check(none == nil and serr:find('Missing Dust'), 'an input that cannot be found: no slots')
    check(slots.key({ name = 'a:b', damage = 2 }) == 'a:b:2' and slots.key(nil) == '', 'slot keys')
end

-- inputs without an amount are set in the bus but not counted
do
    local r = { only = { trigger = 'deficit', catalysts = 1,
        inputs = { { fluid = 'Water' } },
        outputs = { { fluid = 'A', circuit = 1, target = 100 } } } }
    assert(#config.normalize(r) == 0)
    local st = plan.evaluate(r, fluids({ A = 10 }), fluids({}))
    check(st.only.cycles == nil and st.only.low, 'not counted: the recipe is not held back')
end

-- runner with fakes
local function fakeMachine(active)
    local m = { allowed = true, active = active or false, calls = 0, slots = {}, writes = 0 }
    m.setWorkAllowed = function(v) m.allowed = v; m.calls = m.calls + 1 end
    m.circuit = -1
    m.setCircuitConfiguration = function(v) m.circuit = v; return true end
    m.isMachineActive = function() return m.active end
    -- A stocking bus or hatch: slots 1..16, a missing detail clears the slot.
    m.setConfiguration = function(slot, detail)
        if slot < 1 or slot > 16 then error('invalid slot') end
        m.slots[slot], m.writes = detail, m.writes + 1
        return true
    end
    return m
end

local function fakeSetup()
    local c = {
        controller = fakeMachine(true),
        bus = fakeMachine(),
        hatch = fakeMachine(),
        plasma = { hatch = fakeMachine(), neptunium = 'Neptunium Plasma', fermium = 'Fermium Plasma' },
    }
    return c
end

local kRubberSlots = { items = { { name = 'gregtech:gt.metaitem.01', damage = 2010 } },
    fluids = { { name = 'oxygen' }, { name = 'hydrogen' } } }
local kOtherSlots = { items = { { name = 'bw:dust', damage = 47 } }, fluids = {} }
local kFermium, kNeptunium = { name = 'plasma.fermium' }, { name = 'plasma.neptunium' }
-- A decision's slots with the plasma it uses.
local function withPlasma(base, ...)
    return { items = base.items, fluids = base.fluids, plasma = { ... } }
end
local R = { recipe = 'rubber', fermium = false, slots = kRubberSlots }
local O = { recipe = 'other', fermium = false, slots = kOtherSlots }

do
    local c = fakeSetup()
    local r = runner.new(c, 0)
    check(r.phase == 'stopping' and c.controller.allowed == false, 'starts by stopping')

    runner.step(r, R, 1)
    check(r.phase == 'stopping', 'waits while the machine finishes its cycle')
    check(c.bus.allowed == true and c.bus.writes == 0, 'inputs untouched while active')

    c.controller.active = false
    runner.step(r, R, 2)
    check(c.plasma.hatch.allowed == false, 'idle switches every input off')
    check(c.plasma.hatch.slots[1] == nil, 'no plasma in the decision: the plasma hatch stays empty')
    check(r.phase == 'running' and r.running.recipe == 'rubber', 'idle -> running in the same tick')
    check(c.bus.slots[1] and c.bus.slots[1].damage == 2010 and c.bus.slots[2] == nil,
        'bus slots set to the items')
    check(c.hatch.slots[1].name == 'oxygen' and c.hatch.slots[2].name == 'hydrogen',
        'hatch slots set to the fluids')
    check(c.bus.writes == 16 and c.hatch.writes == 16, 'every slot written once at first')
    check(c.bus.allowed and c.hatch.allowed and c.controller.allowed,
        'bus, hatch and controller enabled')

    c.controller.active = true
    local calls = c.bus.calls
    runner.step(r, R, 3)
    check(c.bus.calls == calls, 'no switching while the decision holds')

    runner.step(r, O, 4)
    check(r.phase == 'stopping' and c.controller.allowed == false, 'new decision: stop first')
    check(c.bus.allowed == true and c.bus.slots[1].damage == 2010,
        'old inputs stay until the cycle ends')

    c.controller.active = false
    local busWrites, hatchWrites = c.bus.writes, c.hatch.writes
    runner.step(r, O, 5)
    check(r.running.recipe == 'other' and c.bus.slots[1].damage == 47, 'switched after the cycle ended')
    check(c.bus.writes - busWrites == 1, 'only the slot that differs is written')
    check(c.hatch.writes - hatchWrites == 2 and c.hatch.slots[1] == nil, 'old fluids cleared')
    check(c.hatch.allowed == false, 'a recipe without fluids leaves the hatch off')

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
    c.hatch.setWorkAllowed = function() error('gone') end
    r = runner.new(c, 0)
    runner.step(r, R, 1)
    check(r.phase == 'stopping' and c.controller.allowed == false,
        'failed start falls back to stopping')

    c = fakeSetup()
    c.controller.active = false
    c.bus.setConfiguration = function(slot) if slot == 3 then error('invalid slot') end return true end
    r = runner.new(c, 0)
    runner.step(r, R, 1)
    check(r.phase == 'stopping' and c.bus.allowed == false and c.controller.allowed == false,
        'a slot that cannot be set: no start')
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
    local F = { recipe = 'rubber', fermium = true, slots = withPlasma(kRubberSlots, kFermium) }
    runner.step(r, F, 1)
    check(c.plasma.hatch.allowed == true and c.plasma.hatch.slots[1].name == 'plasma.fermium'
        and c.plasma.hatch.slots[2] == nil, 'fermium decision: the plasma hatch holds fermium only')

    c.controller.active = true
    c.controller.getWorkMaxProgress = function() return 1200 end
    runner.step(r, F, 2)
    check(r.cycleSeconds == 60, 'cycle length read from the controller in seconds')

    runner.step(r, R, 3)
    check(r.phase == 'stopping' and c.plasma.hatch.allowed == true
        and c.plasma.hatch.slots[1].name == 'plasma.fermium', 'dropping fermium waits for the cycle to end')
    c.controller.active = false
    runner.step(r, R, 4)
    check(c.plasma.hatch.allowed == false and c.plasma.hatch.slots[1] == nil
        and r.running.recipe == 'rubber' and r.running.fermium == false,
        'restarted without fermium after the cycle: slot cleared, hatch off')
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
    local N1 = { recipe = 'rubber', fermium = false, neptunium = 1,
        slots = withPlasma(kRubberSlots, kNeptunium) }
    local N3 = { recipe = 'rubber', fermium = false, neptunium = 3,
        slots = withPlasma(kRubberSlots, kNeptunium) }
    -- the circuit must be set before the hatch: check the order inside the hatch call
    local circuitAtEnable
    local hatch = c.plasma.hatch
    local set = hatch.setWorkAllowed
    hatch.setWorkAllowed = function(v)
        if v then circuitAtEnable = c.bus.circuit end
        set(v)
    end
    runner.step(r, N1, 1)
    check(circuitAtEnable == 1 and hatch.allowed, 'circuit set before the neptunium hatch')
    check(hatch.slots[1].name == 'plasma.neptunium', 'the plasma hatch holds neptunium')

    c.controller.active = true
    runner.step(r, N3, 2)
    check(r.phase == 'running' and c.bus.circuit == 3,
        'focus moves on the fly, no stop')

    runner.step(r, R, 3)
    check(r.phase == 'stopping' and c.bus.circuit == 3 and hatch.allowed,
        'neptunium off waits for the cycle, circuit kept while the hatch is on')
    c.controller.active = false
    local circuitAtDisable
    hatch.setWorkAllowed = function(v)
        if not v then circuitAtDisable = c.bus.circuit end
        set(v)
    end
    runner.step(r, R, 4)
    check(circuitAtDisable == 3, 'hatch off before the circuit is reset')
    check(c.bus.circuit == -1 and r.running.neptunium == nil,
        'restarted without neptunium and without circuit')

    c = fakeSetup()
    c.controller.active = false
    c.bus.setCircuitConfiguration = function() return nil, 'no slot' end
    r = runner.new(c, 0)
    runner.step(r, N1, 1)
    check(r.phase == 'stopping' and c.plasma.hatch.allowed == false,
        'refused circuit: never starts neptunium')
end

-- link: settings from the server, telemetry to it
do
    local link = require('qft.link')
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
    local function rs()
        local r = {
            glue = { name = 'Glue', trigger = 'deficit', tier = 1, catalysts = 1,
                inputs = { { fluid = 'Water' } },
                outputs = { { fluid = 'Glue', circuit = 1, target = 100, priority = 2 },
                    { fluid = 'Waste', circuit = 2 } } },
            plat = { name = 'Plat', trigger = 'surplus', tier = 1, catalysts = 1,
                inputs = { { item = 'Ore', amount = 1 } },
                outputs = { { fluid = 'Pt', circuit = 1, weight = 1 }, { fluid = 'Pd', circuit = 2 } } },
        }
        assert(#config.normalize(r) == 0)
        return r
    end

    local d = link.defaults(rs(), 'auto')
    check(d.version == 0 and d.mode == 'auto' and #d.recipes == 2 and d.recipes[1].id == 'glue',
        'defaults: from the config, sorted')
    check(d.recipes[1].outputs[1].target == 100 and d.recipes[1].outputs[2].target == nil,
        'defaults: outputs as configured')
    check(link.check({ mode = 'often' }) == nil, 'a bad mode is refused')
    check(link.check({ mode = 'auto', recipes = { { id = 'glue', outputs = {
        { circuit = 1, target = -1 } } } } }) == nil, 'a negative target is refused')

    local r = rs()
    local warned = {}
    link.apply(r, { version = 3, mode = 'deficit', recipes = {
        { id = 'glue', outputs = { { circuit = 2, target = 50 }, { circuit = 9, target = 1 } } },
        { id = 'plat', outputs = { { circuit = 2, weight = 5 } } },
        { id = 'gone', outputs = {} },
    } }, function(fmt, ...) warned[#warned + 1] = fmt:format(...) end)
    check(r.glue.outputs[1].target == nil and r.glue.outputs[2].target == 50,
        'apply: deficit targets as the server says, an output it leaves out is not tracked')
    check(r.plat.outputs[1].weight == nil and r.plat.outputs[2].weight == 5, 'apply: surplus weights')
    check(#warned == 2, 'apply: an unknown circuit and recipe are warned of')
    link.apply(r, { version = 4, mode = 'auto', recipes = { { id = 'glue', enabled = false, outputs = {} } } },
        function() end)
    check(r.glue.off == true and not r.plat.off, 'apply: a recipe switched off')
    local st = plan.evaluate(r, fluids({ Glue = 0 }), resource.snapshot({ { label = 'Ore', name = 'ore', damage = 0, size = 1e6 } },
        { { label = 'Water', name = 'water', amount = 1e6 } }))
    check(st.glue.disabled and not st.glue.low and plan.choose(st, 'glue') == 'plat',
        'a switched off recipe never runs and a running one yields')
    link.apply(r, { version = 5, mode = 'auto', recipes = { { id = 'glue', enabled = true, outputs = {} } } },
        function() end)
    check(r.glue.off == false, 'apply: and back on')
    check(link.check({ mode = 'auto', recipes = { { id = 'glue', enabled = 'no' } } }) == nil,
        'enabled must be a boolean')

    -- a poll: offer and catalog first; the server's settings are taken and saved
    local saved, requests, replies = nil, {}, {}
    local deps = {
        post = function(path, body)
            requests[#requests + 1] = { path = path, body = body }
            local reply = replies[#requests]
            if reply == false then return nil, 'refused' end
            return reply or '{poll=10}'
        end,
        decode = decode,
        store = { read = function() return saved end, write = function(t) saved = t; return true end,
            encode = encode, decode = decode },
        recipes = rs(), mode = 'auto', name = 'qft-main', coords = { x = 1, y = 2, z = 3 },
        run = 'test-1',
    }
    local l = link.new(deps)
    replies[1] = '{poll=10,settings={version=1,mode="deficit",recipes={{id="glue",outputs={'
        .. '{circuit=1,target=500,priority=1}}}}}}'
    local reply = l:poll({ phase = 'running', mode = 'auto', want = 'glue',
        running = { recipe = 'glue', fermium = true }, cycleSeconds = 6, plasma = { fermium = 900 },
        states = plan.evaluate(deps.recipes, fluids({ Glue = 10 }), fluids({ Water = 1 })) })
    local body = requests[1].body
    check(requests[1].path == '/agent/v1/qft/poll', 'its path')
    check(body:find('"name":"qft-main"', 1, true) and body:find('"x":1', 1, true), 'the setup and coords')
    check(body:find('"offer":{', 1, true) and body:find('"catalog":[', 1, true), 'offer and catalog first')
    check(body:find('"phase":"running"', 1, true) and body:find('"recipe":"glue"', 1, true), 'the state')
    check(body:find('"amount":10', 1, true) ~= nil, 'output amounts')
    check(reply and reply.changed and l.settings.version == 1 and l.settings.mode == 'deficit',
        'the server\'s settings taken')
    check(deps.recipes.glue.outputs[1].target == 500, '... into the recipes')
    check(saved and saved:find('deficit') ~= nil, '... and saved')

    replies[2] = false
    check(l:poll({ phase = 'idle', mode = 'deficit', plasma = {} }) == nil, 'a failed poll: nil')
    l:poll({ phase = 'idle', mode = 'deficit', plasma = {} })
    check(not requests[3].body:find('"catalog"', 1, true) and not requests[3].body:find('"offer"', 1, true),
        'no catalog once one got through, no offer with the server\'s settings')

    local deps2 = {}
    for k, v in pairs(deps) do deps2[k] = v end
    deps2.recipes = rs()
    local l2 = link.new(deps2)
    check(l2.settings.version == 1 and deps2.recipes.glue.outputs[1].target == 500,
        'saved settings at the start: no server needed')
    check(l2.offer.recipes[1].outputs[1].target == 100, 'the offer is still the config\'s')

    -- a copy saved by an older agent lacks a field (enabled): the first poll of a run asks for
    -- the server's settings and takes them even at the same version
    local requestsBefore = #requests
    replies[requestsBefore + 1] = '{poll=10,settings={version=1,mode="deficit",recipes={{id="glue",'
        .. 'enabled=false,outputs={{circuit=1,target=500,priority=1}}}}}}'
    local reply2 = l2:poll({ phase = 'idle', mode = 'deficit', plasma = {} })
    check(requests[requestsBefore + 1].body:find('"settings":-1', 1, true) ~= nil,
        'the first poll of a run reports no version')
    check(reply2 and reply2.changed and deps2.recipes.glue.off == true,
        'the server\'s settings taken though the version is the same')
    l2:poll({ phase = 'idle', mode = 'deficit', plasma = {} })
    check(requests[#requests].body:find('"settings":1', 1, true) ~= nil, 'then the version as usual')
end

print(failures == 0 and 'all tests passed' or (failures .. ' test(s) failed'))
os.exit(failures == 0 and 0 or 1)
