---The black hole of a Pseudostable Black Hole Containment Field as a state machine. Pure logic:
---the runner feeds it what it measured and carries out what it returns.
---
---Mechanics (GT5-Unofficial 5.09.54.264, MTEBlackHoleCompressor; not checked in game):
--- * A seed or collapser is taken from an input bus only in a processing check, so only while
---   work is allowed and no recipe runs. A collapser is handled before the recipe lookup.
--- * Once a second (the Utility Hatch pulse, pulse mode) the stability drops by 1, unless the
---   spacetime hatches hold the current cost: then that much is drained and nothing drops. The
---   cost doubles every 30 such holds and resets when the hole closes.
--- * At stability 0 the hole is unstable: running recipes are voided, and a new recipe still
---   starts. So work is never allowed there.
local hole = {}

---@enum bhc.Phase
hole.Phase = {
    kIdle = 'idle',       -- closed; work not allowed
    kOpening = 'opening', -- a seed is in the bus, waiting for the first pulse
    kOpen = 'open',       -- pulses come; work allowed
    kClosing = 'closing', -- work stopped; collapser goes in once the recipe ends
    kCollapsing = 'collapsing', -- collapser in the bus, waiting for the pulses to stop
}

---@class bhc.Settings
---@field holdAt integer # stability at which holding starts (below 20 the parallels are x4)
---@field floor integer # never let it lower: stop work if a hold failed and it got here
---@field maxHolds integer # holds per session for new work; a running recipe is held anyway
---@field minSpacetime integer # L of spacetime in supplies to open a hole
---@field pulseTimeout number # s without a pulse that mean the hole is closed
---@field catalystTimeout number # s a seed or collapser may lie in the bus untaken

---@class bhc.State
---@field phase bhc.Phase
---@field pulses integer # pulses since the hole opened
---@field holds integer # pulses the spacetime held
---@field unknown boolean? # pulses come but the count was lost: trust nothing, wind down
---@field lastPulse number? # agent clock of the last pulse
---@field since number? # agent clock when the phase started
---@field level integer? # L in the spacetime hatch after the last fill or drain
---@field spent integer # L of spacetime drained by the machine this session
---@field session integer # sessions opened, for the log
---@field probe integer? # unknown count: L to offer per hold, doubled until the machine takes it

---@return bhc.State
function hole.new()
    return { phase = hole.Phase.kIdle, pulses = 0, holds = 0, spent = 0, session = 0 }
end

---Spacetime for the next hold, after `holds` of them.
---@param holds integer
---@return integer
function hole.cost(holds)
    return 1 << (holds // 30)
end

---Spacetime for `holds` holds from the start of a session.
---@param holds integer
---@return integer
function hole.total(holds)
    local q, r = holds // 30, holds % 30
    return 30 * ((1 << q) - 1) + r * (1 << q)
end

---@param s bhc.State
---@return integer
function hole.stability(s)
    return 100 - s.pulses + s.holds
end

---@class bhc.Observation
---@field now number # agent clock, s
---@field pulse boolean # this step is a pulse (rising edge of the Utility Hatch)
---@field level integer # L in the spacetime hatch now
---@field busy boolean # a recipe runs
---@field work boolean # the ingredients subnet has something
---@field supplies { seeds: integer, collapsers: integer, spacetime: integer }? # read when idle
---@field catalyst boolean # the catalyst bus holds anything

---@class bhc.Action
---@field allow boolean? # setWorkAllowed
---@field insert ('seed'|'collapser')?
---@field fill integer? # top the hatch up to this many L
---@field empty boolean? # move all spacetime back to supplies
---@field clearBus boolean? # move whatever is in the catalyst bus back to supplies
---@field warn string?
---@field info string?

local function enter(s, phase, now)
    s.phase = phase
    s.since = now
end

local function wantsHold(s, obs, cfg, closing)
    local stab = hole.stability(s)
    if stab > cfg.holdAt then
        return false
    end
    if obs.busy then
        return true -- the running recipe must end before the hole goes unstable
    end
    return not closing and obs.work and s.holds < cfg.maxHolds
end

---What the next hold costs: the counted cost, or the probe when the count is lost.
local function nextCost(s)
    return s.unknown and (s.probe or 1) or hole.cost(s.holds)
end

---What the hatch must hold before the next pulse.
local function feed(s, obs, cfg, closing, out)
    if wantsHold(s, obs, cfg, closing) then
        local cost = nextCost(s)
        if obs.level < cost then
            out[#out + 1] = { fill = cost }
        end
    elseif obs.level > 0 then
        out[#out + 1] = { empty = true }
    end
end

---Accounts a pulse: did the machine drain the hatch?
local function count(s, obs, out)
    s.pulses = s.pulses + 1
    s.lastPulse = obs.now
    local before = s.level or obs.level
    local drained = before - obs.level
    if drained > 0 then
        if s.unknown then
            -- The machine took what it costs now: that gives the count back, near enough.
            local doublings = math.floor(math.log(drained, 2) + 0.5)
            s.holds = math.max(s.holds, 30 * doublings) + 1
            s.probe = hole.cost(s.holds)
        elseif drained ~= hole.cost(s.holds) then
            out[#out + 1] = { warn = ('hold drained %d L, expected %d')
                :format(drained, hole.cost(s.holds)) }
            s.holds = s.holds + 1
        else
            s.holds = s.holds + 1
        end
        s.spent = s.spent + drained
    elseif s.unknown and before >= (s.probe or 1) then
        s.probe = (s.probe or 1) * 2 -- offered and not taken: the cost is higher
    end
    s.level = obs.level
end

local function suppliesOk(sup, cfg)
    return sup ~= nil and sup.seeds >= 1 and sup.collapsers >= 1
        and sup.spacetime >= cfg.minSpacetime
end

---One step. Mutates `s`, returns the actions in order.
---@param s bhc.State
---@param obs bhc.Observation
---@param cfg bhc.Settings
---@return bhc.Action[]
function hole.step(s, obs, cfg)
    local out = {}
    local P = hole.Phase

    -- A pulse in any phase means the hole is open.
    if obs.pulse and (s.phase == P.kIdle) then
        s.unknown = true
        s.pulses, s.holds, s.spent, s.level, s.probe = 0, 0, 0, obs.level, 1
        enter(s, P.kClosing, obs.now)
        out[#out + 1] = { allow = false }
        out[#out + 1] = { warn = 'the hole is open but the agent did not open it: winding down' }
        return out
    end

    if s.phase == P.kIdle then
        if obs.level > 0 then
            out[#out + 1] = { empty = true }
        end
        if obs.catalyst then
            -- A second catalyst next to a leftover one would be taken in whatever slot order.
            out[#out + 1] = { clearBus = true }
            return out
        end
        if obs.work and suppliesOk(obs.supplies, cfg) then
            s.session = s.session + 1
            s.pulses, s.holds, s.spent, s.level, s.unknown = 0, 0, 0, 0, nil
            enter(s, P.kOpening, obs.now)
            out[#out + 1] = { insert = 'seed' }
            out[#out + 1] = { allow = true }
            out[#out + 1] = { info = ('session %d: seed in'):format(s.session) }
        elseif obs.work and obs.supplies then
            out[#out + 1] = { warn = ('cannot open: %d seeds, %d collapsers, %d L spacetime')
                :format(obs.supplies.seeds, obs.supplies.collapsers, obs.supplies.spacetime) }
        end
        return out
    end

    if s.phase == P.kOpening then
        if obs.pulse then
            enter(s, P.kOpen, obs.now)
            count(s, obs, out)
            out[#out + 1] = { info = ('session %d: open'):format(s.session) }
            feed(s, obs, cfg, false, out)
        elseif obs.now - s.since > cfg.catalystTimeout then
            enter(s, P.kIdle, obs.now)
            out[#out + 1] = { allow = false }
            out[#out + 1] = { warn = 'the seed was not taken: is it in the right bus?' }
        end
        return out
    end

    -- Open, closing or collapsing: a missing pulse means the hole is closed.
    if not obs.pulse then
        if s.lastPulse and obs.now - s.lastPulse > cfg.pulseTimeout then
            local expected = s.phase == P.kCollapsing
            enter(s, P.kIdle, obs.now)
            out[#out + 1] = { allow = false }
            out[#out + 1] = { empty = true }
            out[#out + 1] = { [expected and 'info' or 'warn'] =
                ('session %d closed after %d s, %d holds, %d L spacetime%s'):format(s.session,
                    s.pulses, s.holds, s.spent, expected and '' or ' (not by the agent)') }
        end
        return out
    end

    count(s, obs, out)
    local stab = hole.stability(s)

    if s.phase == P.kOpen then
        if s.unknown or not wantsHold(s, obs, cfg, false) and stab <= cfg.holdAt then
            enter(s, P.kClosing, obs.now)
            out[#out + 1] = { allow = false }
        elseif stab <= cfg.floor and obs.level < hole.cost(s.holds) then
            enter(s, P.kClosing, obs.now)
            out[#out + 1] = { allow = false }
            out[#out + 1] = { warn = ('stability %d and the hatch is short: stopping work')
                :format(stab) }
        else
            feed(s, obs, cfg, false, out)
            return out
        end
    end

    if s.phase == P.kClosing then
        if obs.busy then
            if s.unknown then
                if obs.level < nextCost(s) then
                    out[#out + 1] = { fill = nextCost(s) } -- count lost: hold to the end
                end
            else
                feed(s, obs, cfg, true, out)
            end
        else
            enter(s, P.kCollapsing, obs.now)
            out[#out + 1] = { empty = true }
            out[#out + 1] = { insert = 'collapser' }
            out[#out + 1] = { allow = true }
        end
        return out
    end

    -- Collapsing: the collapser goes in at the next check; nothing must hold meanwhile.
    if obs.busy then
        -- A recipe started before the collapser was taken: hold it to its end again.
        enter(s, P.kClosing, obs.now)
        out[#out + 1] = { allow = false }
        out[#out + 1] = { warn = 'a recipe started before the collapser was taken' }
        feed(s, obs, cfg, true, out)
        return out
    end
    if obs.level > 0 then
        out[#out + 1] = { empty = true }
    end
    if obs.catalyst and obs.now - s.since > cfg.catalystTimeout then
        s.since = obs.now
        out[#out + 1] = { allow = true } -- the check takes it while work is allowed
        out[#out + 1] = { warn = 'the collapser is still in the bus' }
    end
    return out
end

---The runner tells what the hatch holds after it filled or emptied it.
---@param s bhc.State
---@param level integer
function hole.settle(s, level)
    s.level = level
end

---After a restart: `elapsed` s passed since the state was saved (nil: unknown).
---@param s bhc.State
---@param elapsed number?
function hole.resume(s, elapsed)
    local P = hole.Phase
    if s.phase == P.kIdle then
        return
    end
    if elapsed == nil then
        s.unknown = true
        s.probe = hole.cost(s.holds)
    else
        -- Assume no hold in the gap: it can only make the real stability higher.
        s.pulses = s.pulses + math.floor(elapsed)
    end
    s.lastPulse = nil
    s.since = nil
end

return hole
