---Local tests: the state machine and the rig against a fake BHC that follows the source
---(MTEBlackHoleCompressor) second by second. Not installed in game.
---  cd factoryctl/bhc && lua tests/run.lua
package.path = './?.lua;../agent/?.lua;' .. package.path

local hole = require('bhc.hole')
local rig = require('bhc.rig')

local failures = 0
local function check(cond, what)
    if not cond then
        failures = failures + 1
        print('FAIL ' .. what)
    end
end

local function copy(t)
    local out = {}
    for k, v in pairs(t) do
        out[k] = type(v) == 'table' and copy(v) or v
    end
    return out
end
local warnings
local log = {
    wrn = function(fmt, ...) warnings[#warnings + 1] = fmt:format(...) end,
    inf = function() end,
    dbg = function() end,
}

-- cost / total
do
    check(hole.cost(0) == 1 and hole.cost(29) == 1 and hole.cost(30) == 2 and hole.cost(60) == 4,
        'cost doubles every 30 holds')
    check(hole.total(30) == 30 and hole.total(60) == 90 and hole.total(120) == 450,
        'total of the holds')
    check(hole.total(45) == 30 + 15 * 2, 'total mid-block')
end

---A BHC as the source has it, one second per tick() call.
local function newMachine(opts)
    local m = {
        status = 1, stab = 100, mod = 1, counter = 0, slot = nil, bus = {}, allowed = false,
        recipe = nil, jobs = opts.jobs or 0, duration = opts.duration or 20,
        done = 0, voided = 0, seeds = opts.seeds or 5, collapsers = opts.collapsers or 5,
        spacetime = opts.spacetime or 100000, drained = 0, pulse = false,
    }
    function m:close()
        self.status, self.stab, self.mod, self.counter = 1, 100, 1, 0
    end
    function m:tick()
        -- processing check: idle and allowed
        if self.allowed and not self.recipe then
            local cat = table.remove(self.bus, 1)
            if cat == 'seed' and self.status == 1 then
                self.status = 2
            elseif cat == 'collapser' and self.status ~= 1 then
                self:close()
            elseif cat then
                table.insert(self.bus, 1, cat) -- not taken
            end
            if self.status ~= 1 and self.jobs > 0 then
                self.jobs = self.jobs - 1
                self.recipe = { left = self.duration, void = false }
            end
        end
        if self.recipe then
            if self.status == 3 then
                self.recipe.void = true
            end
            self.recipe.left = self.recipe.left - 1
            if self.recipe.left <= 0 then
                if self.recipe.void then
                    self.voided = self.voided + 1
                else
                    self.done = self.done + 1
                end
                self.recipe = nil
            end
        end
        self.pulse = false
        if self.status ~= 1 then
            if self.stab > 0 and self.slot == 'molten.spacetime' and self.spacetime >= self.mod then
                self.spacetime = self.spacetime - self.mod
                self.drained = self.drained + self.mod
                self.counter = self.counter + 1
                if self.counter >= 30 then
                    self.mod, self.counter = self.mod * 2, 0
                end
            else
                self.stab = self.stab - 1
            end
            if self.stab <= 0 then
                self.status = 3
            end
            if self.stab <= -900 then
                self:close()
            else
                self.pulse = true
            end
        end
    end
    return m
end

local kSeed, kCollapser = 32418, 32419

local function deps(m, clock)
    local sides = { pulse = 1, supplies = 2, bus = 4 }
    local hatch = {
        getAutoPull = function() return m.autoPull or false end,
        setAutoPull = function(v) m.autoPull = v end,
        getSlotSize = function() return 16 end,
        getConfiguration = function(slot)
            return slot == 1 and m.slot and { name = m.slot } or nil
        end,
        setConfiguration = function(slot, detail)
            assert(slot == 1)
            m.slot = detail and detail.name or nil
            m.slotWrites = (m.slotWrites or 0) + 1
            return true
        end,
    }
    local transposer = {
        getInventorySize = function(side)
            return side == sides.bus and 4 or side == sides.supplies and 2 or 0
        end,
        getStackInSlot = function(side, slot)
            if side == sides.supplies then
                local count = slot == 1 and m.seeds or m.collapsers
                if count > 0 then
                    return { name = 'gregtech:gt.metaitem.01',
                        damage = slot == 1 and kSeed or kCollapser, size = count }
                end
            elseif side == sides.bus and slot == 4 then
                return { name = 'gregtech:gt.integrated_circuit', damage = 1, size = 1 }
            elseif side == sides.bus and m.bus[slot] then
                return { name = 'gregtech:gt.metaitem.01',
                    damage = m.bus[slot] == 'seed' and kSeed or kCollapser, size = 1 }
            end
        end,
        transferItem = function(src, dst, n, slot)
            if src == sides.supplies and dst == sides.bus then
                if slot == 1 and m.seeds > 0 then
                    m.seeds = m.seeds - 1; m.bus[#m.bus + 1] = 'seed'; return 1
                elseif slot == 2 and m.collapsers > 0 then
                    m.collapsers = m.collapsers - 1; m.bus[#m.bus + 1] = 'collapser'; return 1
                end
            elseif src == sides.bus and dst == sides.supplies and m.bus[slot] then
                local it = table.remove(m.bus, slot)
                if it == 'seed' then m.seeds = m.seeds + 1 else m.collapsers = m.collapsers + 1 end
                return 1
            end
            return 0
        end,
    }
    local controller = {
        getName = function() return 'multimachine.blackholecompressor' end,
        getWorkMaxProgress = function() return m.recipe and m.duration * 20 or 0 end,
        setWorkAllowed = function(v) m.allowed = v end,
    }
    local ingredients = {
        getItemsInNetwork = function() return m.jobs > 0 and { { size = m.jobs } } or {} end,
        getFluidsInNetwork = function() return {} end,
    }
    local supplies = {
        getItemInNetwork = function(_, damage)
            local n = damage == kSeed and m.seeds or m.collapsers
            return n > 0 and { size = n } or nil
        end,
        getFluidInNetwork = function() return { amount = m.spacetime } end,
    }
    local byAddress = { ['bhc-1'] = controller, ['rs-1'] = { address = 'rs-1' },
        ['tp-1'] = transposer, ['ing-1'] = ingredients, ['sup-1'] = supplies, ['mh-1'] = hatch }
    local byKind = { gt_machine = { ['bhc-1'] = 'gt_machine' }, redstone = { ['rs-1'] = 'redstone' },
        transposer = { ['tp-1'] = 'transposer' }, me_inputhatch = { ['mh-1'] = 'me_inputhatch' },
        me_interface = { ['ing-1'] = 'me_interface', ['sup-1'] = 'me_interface' } }
    return {
        list = function(kind) return byKind[kind] or {} end,
        proxy = function(address) return byAddress[address] end,
        config = { ingredients = 'ing', supplies = 'sup', sides = sides, workEvery = 0 },
        clock = clock,
    }
end

local kCfg = { holdAt = 5, floor = 2, maxHolds = 120, minSpacetime = 1000, pulseTimeout = 2.5,
    catalystTimeout = 10 }

---Runs agent and machine together for `seconds`; one pulse step and one plain step a second.
local function run(m, s, seconds, opts)
    opts = opts or {}
    local now = opts.start or 0
    local r = rig.new(deps(m, function() return now end))
    hole.settle(s, r:prepareHatch(log))
    local lowest = 100
    local unsafe = 0
    for _ = 1, seconds do
        m:tick()
        now = now + 1
        if m.recipe and m.status ~= 1 and m.stab <= 0 then
            unsafe = unsafe + 1
        end
        if m.status ~= 1 and m.stab < lowest then
            lowest = m.stab
        end
        if not opts.agentDown then
            r:apply(hole.step(s, r:observe(m.pulse, s.phase, s.holding), kCfg), s, hole, log)
            now = now + 0.001
            r:apply(hole.step(s, r:observe(false, s.phase, s.holding), kCfg), s, hole, log)
        end
    end
    return { lowest = lowest, unsafe = unsafe, rig = r }
end

-- A queue of jobs: opens, holds low, closes after the last one, nothing voided.
do
    warnings = {}
    local m = newMachine({ jobs = 10, duration = 20 })
    local s = hole.new()
    local res = run(m, s, 400)
    check(m.done == 10 and m.voided == 0, ('all jobs done, none voided (%d/%d)'):format(m.done,
        m.voided))
    check(m.status == 1 and s.phase == 'idle', 'closed at the end: ' .. s.phase)
    check(res.lowest >= kCfg.holdAt - 1, 'held near holdAt, lowest ' .. res.lowest)
    check(m.seeds == 4 and m.collapsers == 4, 'one seed and one collapser')
    check(m.slot == nil and #m.bus == 0, 'hatch slot cleared, bus empty')
    check(m.drained == hole.total(s.holds) and s.spent == m.drained,
        ('spent what the holds cost: %d / %d'):format(m.drained, hole.total(s.holds)))
    check(#warnings == 0, 'no warnings: ' .. table.concat(warnings, '; '))
end

-- maxHolds reached with work left: the running recipe is held to its end, then it closes.
do
    warnings = {}
    local m = newMachine({ jobs = 100, duration = 20 })
    local s = hole.new()
    run(m, s, 300)
    check(m.voided == 0, 'nothing voided past maxHolds')
    check(m.collapsers < 5, 'closed after maxHolds')
    check(s.session >= 2, 'opened again for the rest: session ' .. s.session)
end

-- No supplies: does not open.
do
    warnings = {}
    local m = newMachine({ jobs = 3, collapsers = 0 })
    local s = hole.new()
    run(m, s, 20)
    check(m.status == 1 and m.seeds == 5, 'no collapser, no hole')
end

-- A long recipe started at 100: held past the free 95 s, closed after.
do
    warnings = {}
    local m = newMachine({ jobs = 1, duration = 150 })
    local s = hole.new()
    local res = run(m, s, 260)
    check(m.done == 1 and m.voided == 0 and res.unsafe == 0, 'long recipe held to its end')
    check(m.status == 1, 'closed after the long recipe')
end

-- Restart with the time known: the count goes on.
do
    warnings = {}
    local m = newMachine({ jobs = 10, duration = 20 })
    local s = hole.new()
    run(m, s, 60)
    local copy = copy(s)
    run(m, s, 7, { agentDown = true })
    hole.resume(copy, 7)
    run(m, copy, 400, { start = 70 })
    check(m.voided == 0 and m.done == 10, 'resumed session voids nothing')
    check(m.status == 1, 'resumed session closes')
end

-- Restart with the count lost, late in a held session: probes the cost, holds, winds down.
do
    warnings = {}
    local m = newMachine({ jobs = 40, duration = 30 })
    local s = hole.new()
    run(m, s, 200)
    local copy = copy(s)
    hole.resume(copy, nil)
    local res = run(m, copy, 200, { start = 300 })
    check(m.voided == 0 and res.unsafe == 0, 'lost count voids nothing')
    check(copy.session > s.session or m.status == 1, 'lost count: the session is closed')
    check(not copy.unknown, 'a new session counts again')
end

-- A hole the agent did not open: stops work, holds the running recipe, closes.
do
    warnings = {}
    local m = newMachine({ jobs = 2, duration = 120 })
    m.status = 2
    m.allowed = true
    local s = hole.new()
    local res = run(m, s, 300)
    check(m.voided == 0 and res.unsafe == 0, 'foreign hole: nothing voided')
    check(m.status == 1, 'foreign hole closed')
end

-- An unstable hole found with a recipe running: held to its end (nothing is drained at 0), closed.
do
    warnings = {}
    local m = newMachine({ duration = 200 })
    m.status, m.stab, m.allowed = 3, -50, true
    m.recipe = { left = 60, void = true }
    local s = hole.new()
    run(m, s, 120)
    check(m.drained == 0 and m.slot == nil, 'unstable hole: nothing drained, slot cleared')
    check(m.status == 1 and s.phase == 'idle', 'unstable hole closed')
end

-- The hatch as a previous run or the player left it: auto-pull on, our slot set, hole closed.
do
    warnings = {}
    local m = newMachine({})
    m.autoPull, m.slot = true, 'molten.spacetime'
    local s = hole.new()
    run(m, s, 3)
    check(not m.autoPull and m.slot == nil and not s.holding, 'auto-pull off, slot cleared')
end

-- Supplies fall below minSpacetime while holding: no new work, the running recipe is finished.
do
    warnings = {}
    local m = newMachine({ jobs = 30, duration = 20, spacetime = 60 })
    local s = hole.new()
    kCfg.minSpacetime = 40
    local res = run(m, s, 400)
    kCfg.minSpacetime = 1000
    check(res.unsafe == 0, 'dry supplies: nothing runs unstable, lowest ' .. res.lowest)
    check(s.spent == m.drained, ('dry supplies: counted right (%d/%d)'):format(s.spent, m.drained))
    check(m.jobs > 0 and m.status == 1, 'dry supplies: no new work, closed')
end

-- Supplies empty under a long recipe: the missed holds are seen and counted as such.
do
    warnings = {}
    local m = newMachine({ jobs = 1, duration = 150, spacetime = 35 })
    local s = hole.new()
    kCfg.minSpacetime = 10
    run(m, s, 300)
    kCfg.minSpacetime = 1000
    check(s.spent == m.drained, ('empty supplies: counted right (%d/%d)'):format(s.spent, m.drained))
    check(#warnings > 0, 'empty supplies: warned')
    check(m.status == 1, 'empty supplies: closed')
end

-- Restarted in each phase with the clocks gone, the hole meanwhile closed: no error, back to idle.
for _, phase in ipairs({ 'opening', 'open', 'closing', 'collapsing' }) do
    warnings = {}
    local m = newMachine({})
    local s = hole.new()
    s.phase, s.session, s.since, s.lastPulse = phase, 1, 12345, 12345
    hole.resume(s, 30)
    local ok, err = pcall(run, m, s, 20)
    check(ok, ('resumed in %s: %s'):format(phase, tostring(err)))
    check(s.phase == 'idle', ('resumed in %s on a closed hole: idle, is %s'):format(phase, s.phase))
end

-- A seed nobody takes (wrong bus): back to idle, the bus is cleared, tried again.
do
    warnings = {}
    local m = newMachine({ jobs = 1 })
    local s = hole.new()
    local tick = m.tick
    m.tick = function(self)
        local allowed = self.allowed
        self.allowed = false -- the machine never checks
        tick(self)
        self.allowed = allowed
    end
    run(m, s, 15)
    check(s.phase ~= 'open', 'not open without the machine')
    run(m, s, 3)
    check(#m.bus <= 1, 'never two catalysts in the bus')
end

-- Stocking the supplies interface: fills empty slots, keeps what is right, never overwrites.
do
    local m = newMachine({})
    local d = deps(m, function() return 0 end)
    local cfgItems, sets = {}, 0
    local sup = d.proxy('sup-1')
    sup.getInterfaceConfiguration = function(slot) return cfgItems[slot] end
    sup.setInterfaceConfiguration = function(slot, t) cfgItems[slot] = t; sets = sets + 1 end
    d.config.stock = { seedSlot = 1, seeds = 2, collapserSlot = 2, collapsers = 2 }
    local r = rig.new(d)
    check(r:stock(log) and sets == 2, 'stocks the empty slots')
    check(cfgItems[1].damage == kSeed and cfgItems[2].damage == kCollapser
        and cfgItems[2].size == 2, 'stocks the right things')
    check(r:stock(log) and sets == 2, 'leaves a right config alone')
    cfgItems[2] = { name = 'minecraft:stone', damage = 0, size = 64 }
    local ok, why = r:stock(log)
    check(not ok and why:find('slot 2') and cfgItems[2].name == 'minecraft:stone',
        'keeps a foreign item: ' .. tostring(why))
    d.config.stock = nil
end

if failures == 0 then
    print('all bhc tests passed')
else
    print(failures .. ' failure(s)')
    os.exit(1)
end
