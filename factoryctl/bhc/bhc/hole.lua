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
---   Only hatches behind the lasers (hint 2 of the structure) hold; elsewhere a hatch is a recipe
---   input, so the slot set there feeds the recipes instead.
--- * A hold is never assumed: the supplies must lose exactly its cost by the next pulse. One test
---   hold at opening proves the whole path (place, network, toggle bus, fluid name) at 99.
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
---@field holdAt integer # stability at which holding starts: above the longest recipe, in s,
---                      # so that the running one ends after a missed hold
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
---@field spacetime integer? # L in supplies at the last pulse: what a hold is measured from
---@field low boolean? # the supplies are below minSpacetime: no new work
---@field blocked boolean? # a hold went wrong: no new hole until the agent restarts
---@field spent integer # L of spacetime drained by the machine this session
---@field session integer # sessions opened, for the log
---@field recipes integer # recipes started this session
---@field busy boolean? # a recipe ran at the last step: a new one is counted on the edge

---@return bhc.State
function hole.new()
    return { phase = hole.Phase.kIdle, pulses = 0, holds = 0, spent = 0, session = 0,
        recipes = 0 }
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
---@field spacetime integer? # L in supplies, read on pulses while the hole is open
---@field busy boolean # a recipe runs
---@field work boolean # the ingredients subnet has something
---@field supplies { seeds: integer, collapsers: integer, spacetime: integer }? # read when idle
---@field catalyst boolean # the catalyst bus holds anything

---@class bhc.Action
---@field allow boolean? # setWorkAllowed
---@field insert ('seed'|'collapser')?
---@field hold boolean? # set (true) or clear (false) the stocking slot
---@field clearBus boolean? # move whatever is in the catalyst bus back to supplies
---@field session bhc.Summary? # a session ended: for the server
---@field warn string?
---@field info string?

---@class bhc.Summary
---@field session integer
---@field seconds integer # pulses: the seconds the hole was open
---@field holds integer
---@field spent integer # L of spacetime the holds took
---@field recipes integer # recipes started
---@field ending 'collapsed'|'closed'|'seed' # by the agent's collapser; not by it; seed not taken
---@field unknown boolean # the count was lost (a restart without the time)
---@field blocked boolean # a hold went wrong

local function summary(s, ending)
    return { session = s.session, seconds = s.pulses, holds = s.holds, spent = s.spent,
        recipes = s.recipes or 0, ending = ending, unknown = s.unknown == true,
        blocked = s.blocked == true }
end

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

---Accounts a pulse by what the supplies lost since the last one: exactly the cost is a hold,
---less is a missed one, more is a hold with something else using the supplies. Returns false
---when anything but exactly the cost was lost. With the count lost the cost is not known, so
---neither is the stability: any loss is taken as a hold.
local function count(s, obs, cfg, out)
    local stable = hole.stability(s) > 0 -- before this pulse: the machine drains only then
    s.pulses = s.pulses + 1
    s.lastPulse = obs.now
    local before, now = s.spacetime, obs.spacetime
    s.spacetime = now
    if now then
        s.low = now < cfg.minSpacetime or nil
    end
    if not s.holding or before == nil or now == nil then
        return true -- no hold due, or nothing to measure it by (after a restart): taken as none
    end
    local lost = before - now
    if s.unknown then
        if lost > 0 then
            s.holds, s.spent = s.holds + 1, s.spent + lost
        end
        return true
    end
    if not stable then
        return true -- no drain on an unstable hole
    end
    local cost = hole.cost(s.holds)
    if lost >= cost then
        -- The machine pays all or nothing: more than the cost is the hold and someone else.
        s.holds, s.spent = s.holds + 1, s.spent + cost
        if lost == cost then
            return true
        end
        out[#out + 1] = { warn = ('the supplies lost %d L in a hold of %d L: something else uses'
            .. ' them'):format(lost, cost) }
        return false
    end
    local why
    if lost ~= 0 then
        why = ('the supplies lost %d L, less than the %d L of the hold: something else uses them')
            :format(lost, cost)
    elseif s.holds == 0 then
        why = 'the spacetime hatch drained nothing: is it behind a laser (hint 2 of the'
            .. ' structure)? Is it connected, is the spacetime there?'
    else
        why = ('hold %d of %d L missed: the supplies lost nothing'):format(s.holds + 1, cost)
    end
    out[#out + 1] = { warn = why }
    return false
end

---A hold went wrong: what is wrong stays so, and a hole each time would burn the catalysts.
local function block(s, out)
    if not s.blocked then
        s.blocked = true
        out[#out + 1] = { warn = 'no new hole until the agent is restarted' }
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
    if s.phase ~= P.kIdle then
        -- After a restart the old clock means nothing: time the phase and the pulses from now.
        s.since = s.since or obs.now
        s.lastPulse = s.lastPulse or obs.now
        if obs.busy and not s.busy then
            s.recipes = (s.recipes or 0) + 1
        end
        s.busy = obs.busy
    end

    -- A pulse in any phase means the hole is open.
    if obs.pulse and (s.phase == P.kIdle) then
        s.unknown = true
        s.pulses, s.holds, s.spent, s.spacetime = 0, 0, 0, nil
        s.session, s.recipes, s.busy = s.session + 1, obs.busy and 1 or 0, obs.busy
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
        if s.blocked then
            return out
        end
        if obs.work and suppliesOk(obs.supplies, cfg) then
            s.session = s.session + 1
            s.pulses, s.holds, s.spent, s.unknown, s.spacetime, s.low = 0, 0, 0, nil, nil, nil
            s.recipes, s.busy = 0, false
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
            setHold(s, not s.unknown, out) -- the test hold
        elseif obs.now - s.since > cfg.catalystTimeout then
            enter(s, P.kIdle, obs.now)
            out[#out + 1] = { allow = false }
            out[#out + 1] = { warn = 'the seed was not taken: is it in the right bus?' }
            out[#out + 1] = { session = summary(s, 'seed') }
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
            out[#out + 1] = { session = summary(s, expected and 'collapsed' or 'closed') }
        end
        return out
    end

    local held = count(s, obs, cfg, out)
    if not held then
        block(s, out)
    end
    local stab = hole.stability(s)

    if s.phase == P.kOpen then
        if not held then
            enter(s, P.kClosing, obs.now) -- the running recipe is still held: the miss may pass
            out[#out + 1] = { allow = false }
        elseif s.unknown or not wantsHold(s, obs, cfg, false) and stab <= cfg.holdAt then
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
    s.blocked = nil -- what blocked the old run may be fixed now
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
    s.spacetime = nil -- the gap may have holds in it
end

return hole
