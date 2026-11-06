---The black hole of a Pseudostable Black Hole Containment Field as a state machine. Pure logic:
---the runner feeds it what it measured and carries out what it returns.
---
---Mechanics (GT5-Unofficial 5.09.54.264, MTEBlackHoleCompressor; not checked in game):
--- * A seed or collapser is taken from an input bus only in a processing check, so only while
---   work is allowed and no recipe runs. A collapser is handled before the recipe lookup.
--- * Once a second (the Utility Hatch pulse, pulse mode) the stability drops by 1, unless the
---   spacetime hatches hold the current cost: then that much is drained and nothing drops. The
---   cost doubles every 30 such holds and resets when the hole closes.
--- * The spacetime hatch here is a stocking hatch on the supplies subnet: with its slot set to
---   spacetime the machine drains the network itself (MTEHatchInputME.drain outside a recipe), so
---   a hold is the slot set before a pulse. A plain input hatch cannot be emptied from outside.
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
---@field minSpacetime integer # L of spacetime in supplies to open a hole and to take new work
---@field pulseTimeout number # s without a pulse that mean the hole is closed
---@field catalystTimeout number # s a seed or collapser may lie in the bus untaken

---@class bhc.State
---@field phase bhc.Phase
---@field pulses integer # pulses since the hole opened
---@field holds integer # pulses the spacetime held
---@field unknown boolean? # pulses come but the count was lost: trust nothing, wind down
---@field lastPulse number? # agent clock of the last pulse
---@field since number? # agent clock when the phase started
---@field holding boolean? # the stocking slot is set: the next pulse is a hold
---@field short boolean? # the supplies cannot pay the next hold
---@field low boolean? # the supplies are below minSpacetime: no new work
---@field spent integer # L of spacetime drained by the machine this session
---@field session integer # sessions opened, for the log

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
---@field spacetime integer? # L in supplies, read on pulses while holding
---@field busy boolean # a recipe runs
---@field work boolean # the ingredients subnet has something
---@field supplies { seeds: integer, collapsers: integer, spacetime: integer }? # read when idle
---@field catalyst boolean # the catalyst bus holds anything

---@class bhc.Action
---@field allow boolean? # setWorkAllowed
---@field insert ('seed'|'collapser')?
---@field hold boolean? # set (true) or clear (false) the stocking slot
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
    -- New work only while the supplies keep `minSpacetime`: the rest finishes what runs.
    return not closing and obs.work and s.holds < cfg.maxHolds and not s.low
end

local function setHold(s, on, out)
    if (s.holding or false) ~= on then
        out[#out + 1] = { hold = on }
    end
end

---Sets the slot for the next pulse.
local function feed(s, obs, cfg, closing, out)
    setHold(s, wantsHold(s, obs, cfg, closing), out)
end

---Accounts a pulse: was it a hold? With the count lost the cost is not known, so neither is the
---stability: only `holds` and `pulses` are kept as they come.
local function count(s, obs, cfg, out)
    s.pulses = s.pulses + 1
    s.lastPulse = obs.now
    if not s.holding or not s.unknown and hole.stability(s) <= 0 then
        return -- no drain on an unstable hole
    end
    local cost = hole.cost(s.holds)
    if s.short and not s.unknown then
        s.short = obs.spacetime ~= nil and obs.spacetime < cost or nil
        out[#out + 1] = { warn = ('supplies held less than the %d L of the hold: missed'):format(
            cost) }
        return
    end
    s.holds = s.holds + 1
    s.spent = s.spent + cost
    -- What is left after this drain pays the next hold or not.
    s.short = obs.spacetime ~= nil and obs.spacetime < hole.cost(s.holds) or nil
    if obs.spacetime then
        s.low = obs.spacetime < cfg.minSpacetime or nil
    end
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
        s.pulses, s.holds, s.spent = 0, 0, 0
        enter(s, P.kClosing, obs.now)
        out[#out + 1] = { allow = false }
        out[#out + 1] = { warn = 'the hole is open but the agent did not open it: winding down' }
        return out
    end

    if s.phase == P.kIdle then
        setHold(s, false, out)
        if obs.catalyst then
            -- A second catalyst next to a leftover one would be taken in whatever slot order.
            out[#out + 1] = { clearBus = true }
            return out
        end
        if obs.work and suppliesOk(obs.supplies, cfg) then
            s.session = s.session + 1
            s.pulses, s.holds, s.spent, s.unknown, s.short, s.low = 0, 0, 0, nil, nil, nil
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
            count(s, obs, cfg, out)
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
            setHold(s, false, out)
            out[#out + 1] = { [expected and 'info' or 'warn'] =
                ('session %d closed after %d s, %d holds, %d L spacetime%s'):format(s.session,
                    s.pulses, s.holds, s.spent, expected and '' or ' (not by the agent)') }
        end
        return out
    end

    count(s, obs, cfg, out)
    local stab = hole.stability(s)

    if s.phase == P.kOpen then
        if s.unknown or not wantsHold(s, obs, cfg, false) and stab <= cfg.holdAt then
            enter(s, P.kClosing, obs.now)
            out[#out + 1] = { allow = false }
        elseif stab <= cfg.floor then
            enter(s, P.kClosing, obs.now)
            out[#out + 1] = { allow = false }
            out[#out + 1] = { warn = ('stability %d: holds were missed, stopping work')
                :format(stab) }
        else
            feed(s, obs, cfg, false, out)
            return out
        end
    end

    if s.phase == P.kClosing then
        if obs.busy then
            if s.unknown then
                setHold(s, true, out) -- count lost: hold the recipe to its end at any cost
            else
                feed(s, obs, cfg, true, out)
            end
        else
            enter(s, P.kCollapsing, obs.now)
            setHold(s, false, out)
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
    setHold(s, false, out)
    if obs.catalyst and obs.now - s.since > cfg.catalystTimeout then
        s.since = obs.now
        out[#out + 1] = { allow = true } -- the check takes it while work is allowed
        out[#out + 1] = { warn = 'the collapser is still in the bus' }
    end
    return out
end

---The runner tells whether the stocking slot is set now.
---@param s bhc.State
---@param holding boolean
function hole.settle(s, holding)
    s.holding = holding
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
    else
        -- Assume no hold in the gap: it can only make the real stability higher.
        s.pulses = s.pulses + math.floor(elapsed)
    end
    s.lastPulse = nil
    s.since = nil
end

return hole
