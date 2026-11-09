---Drives the QFT from one decision to another; a decision is a recipe plus the plasma it uses.
---Inputs are switched only while the machine stands still: plasma is consumed during the run,
---and losing it mid-run spoils the cycle.
---
---GTNH 2.9: one stocking input bus and one hatch serve every recipe, one more hatch the plasma.
---Before a recipe starts their slots are set to its inputs and to the plasma it uses (only the
---slots that differ: every call costs a tick), the Neptunium circuit goes into the bus. On 2.9 Neptunium burns no plasma without a circuit (GT5-Unofficial
---5.09.54.264); the circuit still goes in before the hatches are enabled and out only after they
---are off. Moving the focus while running only changes the circuit: at worst one cycle runs with
---the old focus.
---
---  running --decision changed--> stopping --machine inactive--> idle --decision set--> running
---
---Stopping relies on the controller: setWorkAllowed(false) lets the current cycle finish
---and starts no new one. The runner starts in `stopping`, so whatever the previous run left
---enabled is switched off safely before anything else happens.
local log = require('qft.log')
local slots = require('qft.slots')

local runner = {}

-- Warn when the machine stays inactive this long while it should run (seconds):
-- inputs did not arrive, no power, or the recipe does not match.
local kStallWarn = 120
local kTicksPerSecond = 20

---@class qft.Decision
---@field recipe string
---@field fermium boolean
---@field neptunium integer? # Focused circuit; nil = Neptunium off.
---@field slots qft.Slots # What the bus and the hatch hold for the recipe.

---@alias qft.Phase 'stopping' | 'idle' | 'running'

---@class qft.Runner
---@field components table # Resolved components (proxies), see config/components.lua.
---@field phase qft.Phase
---@field running qft.Decision? # What is enabled; nil unless running.
---@field cycleSeconds number? # Length of the current cycle, read while running.
---@field since number # When the phase was entered.
---@field lastActive number # Last time the machine was seen active.
---@field warned boolean # Stall warning already printed for this phase.
---@field configured { bus: table<integer, string>, hatch: table<integer, string>,
---                  plasma: table<integer, string> } # Slot keys as set; unknown at the start,
---                  # so every slot is written once.

---Calls a component method. OC proxies take no self, so this is proxy.method(...), not ':'.
---A failing call is logged and reported, it does not kill the program.
---@return boolean ok
---@return any result
---@return any err # the method's second result (a reason, by OC's convention)
local function call(proxy, what, method, ...)
    local ok, res, err = pcall(proxy[method], ...)
    if not ok then
        log.err('%s.%s failed: %s', what, method, tostring(res))
    end
    return ok, res, err
end

---setCircuitConfiguration returns true, or nil and a message: both mean "did it work".
---@return boolean
local function setCircuit(bus, what, circuit)
    local ok, res, err = call(bus, what, 'setCircuitConfiguration', circuit)
    if ok and res ~= true then
        log.err('%s.setCircuitConfiguration(%d) refused: %s', what, circuit, tostring(err))
        return false
    end
    return ok
end

---@param a qft.Decision?
---@param b qft.Decision?
---@return boolean
function runner.same(a, b)
    if a == nil or b == nil then
        return a == b
    end
    -- The focus itself may change on the fly; only switching Neptunium on/off needs a stop.
    return a.recipe == b.recipe and a.fermium == b.fermium
        and (a.neptunium == nil) == (b.neptunium == nil)
end

---@param d qft.Decision?
---@return string
function runner.describe(d)
    if not d then
        return 'nothing'
    end
    return d.recipe .. (d.fermium and ' + fermium' or '')
        .. (d.neptunium and (' + neptunium@' .. d.neptunium) or '')
end

---Every input the program may enable: the bus, the hatch and the plasma hatch.
---@return { proxy: table, what: string }[]
local function allInputs(components)
    return { { proxy = components.bus, what = 'bus' }, { proxy = components.hatch, what = 'hatch' },
        { proxy = components.plasma.hatch, what = 'plasma' } }
end

---The proxy behind a slot set: the bus, the hatch or the plasma hatch.
local function holder(r, which)
    return which == 'plasma' and r.components.plasma.hatch or r.components[which]
end

---Sets the slots of the bus, the hatch or the plasma hatch to `list` (slot i = list[i], the rest
---cleared); only the slots that differ from what was set. A failed call leaves that slot unknown.
---@param r qft.Runner
---@param which 'bus' | 'hatch' | 'plasma'
---@param list table[]
---@return boolean ok
local function configure(r, which, list)
    local proxy, set = holder(r, which), r.configured[which]
    for i = 1, slots.kSize do
        local want = list[i]
        local key = slots.key(want)
        if set[i] ~= key then
            local ok, res
            if want then
                ok, res = call(proxy, which, 'setConfiguration', i, want)
            else
                ok, res = call(proxy, which, 'setConfiguration', i)
            end
            if not ok or res == false then
                set[i] = nil
                log.err('%s slot %d: not set to %s', which, i, key ~= '' and key or 'nothing')
                return false
            end
            set[i] = key
        end
    end
    return true
end

---@param r qft.Runner
---@param phase qft.Phase
---@param now number
local function enter(r, phase, now)
    log.dbg('phase %s -> %s', r.phase or '-', phase)
    r.phase, r.since, r.lastActive, r.warned = phase, now, now, false
end

---@param r qft.Runner
---@param now number
local function beginStop(r, now)
    call(r.components.controller, 'controller', 'setWorkAllowed', false)
    enter(r, 'stopping', now)
end

---@param components table
---@param now number
---@return qft.Runner
function runner.new(components, now)
    local r = { components = components, configured = { bus = {}, hatch = {}, plasma = {} } }
    beginStop(r, now)
    return r
end

---@param r qft.Runner
---@param now number
---@return boolean? active # nil when the call failed.
local function machineActive(r, now)
    local ok, active = call(r.components.controller, 'controller', 'isMachineActive')
    if not ok then
        return nil
    end
    if active then
        r.lastActive = now
    elseif r.phase == 'running' and not r.warned and now - r.lastActive > kStallWarn then
        r.warned = true
        log.wrn('QFT inactive for %.0fs while running %s', now - r.lastActive,
            runner.describe(r.running))
    end
    return active
end

---Enables the inputs of a decision and lets the controller work.
---@param r qft.Runner
---@param want qft.Decision
---@param now number
---@return boolean ok # false: something failed, the runner went back to stopping.
local function start(r, want, now)
    local c = r.components
    -- Slots and circuit first, with everything still off.
    local plasma = want.slots.plasma or {}
    if not (configure(r, 'bus', want.slots.items) and configure(r, 'hatch', want.slots.fluids)
            and configure(r, 'plasma', plasma) and setCircuit(c.bus, 'bus', want.neptunium or -1)) then
        beginStop(r, now)
        return false
    end
    local steps = { { c.bus, 'bus' } }
    if #want.slots.fluids > 0 then
        steps[#steps + 1] = { c.hatch, 'hatch' }
    end
    -- The plasma hatch holds only the plasma of this decision: none, it stays off.
    if #plasma > 0 then
        steps[#steps + 1] = { c.plasma.hatch, 'plasma' }
    end
    steps[#steps + 1] = { r.components.controller, 'controller' }

    for _, s in ipairs(steps) do
        if not call(s[1], s[2], 'setWorkAllowed', true) then
            beginStop(r, now)
            return false
        end
    end
    r.running = { recipe = want.recipe, fermium = want.fermium, neptunium = want.neptunium,
        slots = want.slots }
    r.cycleSeconds = nil
    enter(r, 'running', now)
    log.inf('started %s', runner.describe(want))
    return true
end

---Advances the machine one tick towards `want` (nil = stand still).
---Several transitions may happen in one call: running -> stopping -> idle -> running.
---@param r qft.Runner
---@param want qft.Decision?
---@param now number
function runner.step(r, want, now)
    if r.phase == 'running' then
        if not runner.same(want, r.running) then
            log.inf('stopping %s for %s', runner.describe(r.running), runner.describe(want))
            beginStop(r, now)
        else
            ---@cast want -nil # runner.same is false for nil
            if want.neptunium ~= r.running.neptunium then
                local id = r.running.recipe
                if setCircuit(r.components.bus, 'bus', want.neptunium) then
                    log.inf('%s: neptunium focus %d -> %d', id, r.running.neptunium, want.neptunium)
                    r.running.neptunium = want.neptunium
                end
            end
            local plasma = r.running.fermium or r.running.neptunium
            if machineActive(r, now) and plasma then
                -- The plasma reserve has to cover the cycle in progress.
                local ok, ticks = call(r.components.controller, 'controller', 'getWorkMaxProgress')
                if ok and type(ticks) == 'number' and ticks > 0 then
                    r.cycleSeconds = ticks / kTicksPerSecond
                end
            end
            return
        end
    end

    if r.phase == 'stopping' then
        local active = machineActive(r, now)
        if active ~= false then
            return -- still finishing its cycle, or the call failed: try again next tick
        end
        for _, input in ipairs(allInputs(r.components)) do
            call(input.proxy, input.what, 'setWorkAllowed', false)
        end
        -- Only now, with every hatch off, the circuit may go. The slots stay: the next start
        -- changes only those that differ.
        setCircuit(r.components.bus, 'bus', -1)
        r.running, r.cycleSeconds = nil, nil
        enter(r, 'idle', now)
    end

    if r.phase == 'idle' and want then
        start(r, want, now)
    end
end

---Called on exit: the current cycle finishes, no new one starts. Inputs are left as they are;
---the next run switches them off in its own `stopping` phase.
---@param r qft.Runner
function runner.shutdown(r)
    call(r.components.controller, 'controller', 'setWorkAllowed', false)
end

return runner
