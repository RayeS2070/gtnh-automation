---factoryctl black hole agent: runs the black hole of a Pseudostable Black Hole Containment Field
---for its ingredients subnet. Opens a hole with a seed when there is work, holds it with spacetime
---low (x4 parallels) while there is work, and closes it with a collapser after the last recipe.
---See config.lua for the hardware. Ctrl+C stops it: no new recipes start, the running one is
---not held any more.
---  main
-- OpenOS keeps required modules in package.loaded until reboot: drop ours so an update runs.
for name in pairs(package.loaded) do
    if name:match('^fc%.') or name:match('^bhc%.') or name == 'config' then
        package.loaded[name] = nil
    end
end

local shell = require('shell')
-- This program's directory: bhc/ and config.lua are here, the shared fc/ in ../agent.
local here = (debug.getinfo(1, 'S').source:match('^[@=](/.*)/[^/]*$'))
    or shell.getWorkingDirectory()
package.path = ('%s/?.lua;%s/../agent/?.lua;%s'):format(here, here, package.path)

local component = require('component')
local computer = require('computer')
local event = require('event')
local log = require('fc.log')
local hole = require('bhc.hole')
local rig = require('bhc.rig')
local store = require('bhc.store')
local config = require('config')

local kIdleEvery = 5      -- s between checks while closed
local kOpenEvery = 0.5    -- s: how soon a missing pulse is noticed
local kCostReportEvery = 300 -- saves between reports of what a save costs
local kFailuresToStopBeat = 5 -- failed steps in a row that stop the heartbeat: a stuck agent
                              -- must not keep the watchdog quiet

local function settings()
    return {
        holdAt = config.holdAt or 5,
        floor = config.floor or 2,
        maxHolds = config.maxHolds or 120,
        minSpacetime = config.minSpacetime or 10000,
        pulseTimeout = 2.5,
        catalystTimeout = 10,
    }
end

local function main()
    log.clock = computer.uptime
    local levels = { dbg = log.Level.Dbg, inf = log.Level.Inf, wrn = log.Level.Wrn,
        err = log.Level.Err }
    log.setLevel(levels[config.logLevel] or log.Level.Inf)

    local ok, r = pcall(rig.new, {
        list = function(kind) return component.list(kind, true) end,
        proxy = component.proxy,
        config = config,
        clock = computer.uptime,
    })
    if not ok then
        log.err('%s', tostring(r))
        return 1
    end
    local okStock, stocked, why = pcall(r.stock, r, log)
    if not okStock or not stocked then
        log.wrn('supplies interface not stocked: %s', tostring(okStock and why or stocked))
    end
    local okHatch, holding = pcall(r.prepareHatch, r, log)
    if not okHatch then
        log.err('spacetime hatch: %s', tostring(holding))
        return 1
    end
    local cfg = settings()
    local path = config.statePath or store.defaultPath

    local s = hole.new()
    local saved, err = store.load(path)
    if err then
        log.wrn('%s: starting as if closed', err)
    elseif saved then
        s = saved.state
        local elapsed = store.elapsed(saved, computer.uptime(), os.time())
        hole.resume(s, elapsed)
        log.inf('resumed %s, session %d, stability ~%d%s', s.phase, s.session,
            hole.stability(s), elapsed and '' or ' (time lost: winding down)')
    end
    hole.settle(s, holding)

    local cost = store.newCost()
    local function save()
        local t0 = computer.uptime()
        local okSave, why = store.save(path, { state = s, uptime = computer.uptime(),
            time = os.time() })
        store.account(cost, computer.uptime() - t0)
        if not okSave then
            log.wrn('state not saved: %s', tostring(why))
        end
        if cost.count % kCostReportEvery == 0 then
            log.inf('state saves: %d, %.3f s on average, %.3f s worst', cost.count,
                cost.total / cost.count, cost.worst)
        end
    end

    local function step(pulse)
        local phase = s.phase
        local obs = r:observe(pulse, s.phase, s.holding)
        local actions = hole.step(s, obs, cfg)
        r:apply(actions, s, hole, log)
        if pulse or s.phase ~= phase then
            save()
        end
        if pulse then
            log.dbg('%s: stability ~%d, holds %d, holding %s, busy %s', s.phase,
                hole.stability(s), s.holds, tostring(s.holding), tostring(obs.busy))
        end
    end

    local first = r:observe(false, s.phase)
    log.inf('start: %s, holding %s, busy %s, work %s, catalyst in bus %s', s.phase,
        tostring(s.holding), tostring(first.busy), tostring(first.work), tostring(first.catalyst))
    -- Heartbeat: a square wave on a side of the Redstone I/O while the agent runs, for a hardware
    -- watchdog that cuts the spacetime hatch off when it stops.
    local beatSide, beatHalf = config.sides.heartbeat, (config.heartbeatEvery or 1) / 2
    local beatHigh, nextBeat, failures = false, 0, 0
    local function beat(now)
        if beatSide and now >= nextBeat and failures < kFailuresToStopBeat then
            beatHigh = not beatHigh
            nextBeat = now + beatHalf
            pcall(r.redstone.setOutput, beatSide, beatHigh and 255 or 0)
        end
    end

    log.inf('black hole agent; Ctrl+C to stop')
    local nextCheck = 0
    while true do
        local every = s.phase == hole.Phase.kIdle and kIdleEvery or kOpenEvery
        local wait = math.min(every, math.max(0.05, nextCheck - computer.uptime()))
        if beatSide then
            wait = math.min(wait, math.max(0.05, nextBeat - computer.uptime()))
        end
        local name, address, side, old, new = event.pull(wait)
        if name == 'interrupted' then
            break
        end
        beat(computer.uptime())
        local pulse = name == 'redstone_changed' and r:isPulse(address, side, old, new)
        local now = computer.uptime()
        if pulse or now >= nextCheck then
            nextCheck = now + every
            -- A bug in a step must not kill the agent; the traceback says where it was.
            local okStep, why = xpcall(step, debug.traceback, pulse)
            if okStep then
                failures = 0
            else
                failures = failures + 1
                log.err('step failed: %s', tostring(why))
                if failures == kFailuresToStopBeat then
                    log.err('%d steps failed in a row: work stopped, the hold let go, the heartbeat'
                        .. ' stops', failures)
                    -- Nobody counts the pulses now: no recipe may start on a hole left to decay.
                    pcall(r.controller.setWorkAllowed, false)
                    if pcall(r.hold, r, false) then
                        hole.settle(s, false)
                    end
                    if beatSide then
                        pcall(r.redstone.setOutput, beatSide, 0)
                    end
                end
            end
        end
    end

    if beatSide then
        pcall(r.redstone.setOutput, beatSide, 0)
    end
    pcall(r.controller.setWorkAllowed, false)
    -- A slot left set would drain the supplies at a cost doubling every 30 s.
    if pcall(r.hold, r, false) then
        hole.settle(s, false)
    else
        log.err('the spacetime hatch slot may still be set: clear it by hand')
    end
    if s.phase ~= hole.Phase.kIdle then
        log.wrn('stopped with the hole %s at stability ~%d: work is stopped, nothing holds it',
            s.phase, hole.stability(s))
    else
        log.inf('stopped')
    end
    save()
    return 0
end

return main()
