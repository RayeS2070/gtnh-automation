---factoryctl mining agent: runs the Space Elevator mining modules of this computer as the
---factoryctl server plans them. Needs an Internet Card, Adapters at the module controllers and
---their stocking buses and hatches, a transposer and a pool interface per module for its drone
---(config.lua), and the storage buses of the routing. Ctrl+C stops it; the modules end their
---operation and stand.
---  main
-- OpenOS keeps required modules in package.loaded until reboot: drop ours so an update runs.
for name in pairs(package.loaded) do
    if name:match('^fc%.') or name:match('^mm%.') or name == 'config' then
        package.loaded[name] = nil
    end
end

local shell = require('shell')
-- This program's directory: mm/ and config.lua are here, the shared fc/ in ../agent.
local here = (debug.getinfo(1, 'S').source:match('^[@=](/.*)/[^/]*$'))
    or shell.getWorkingDirectory()
package.path = ('%s/?.lua;%s/../agent/?.lua;%s'):format(here, here, package.path)

local component = require('component')
local computer = require('computer')
local event = require('event')
local serialization = require('serialization')
local http = require('fc.http')
local log = require('fc.log')
local logbuf = require('fc.logbuf')
local miner = require('mm.miner')
local config = require('config')

local kRetry = 10 -- s after a step that threw

local function main()
    log.clock = computer.uptime
    local levels = { dbg = log.Level.Dbg, inf = log.Level.Inf, wrn = log.Level.Wrn,
        err = log.Level.Err }
    log.setLevel(levels[config.logLevel] or log.Level.Inf)
    local logs = logbuf.new()
    log.setForwardLevel(levels[config.forwardLevel] or log.Level.Inf)
    log.forward = function(level, text) logs:push(computer.uptime(), level, text) end
    if not component.isAvailable('internet') then
        log.err('no Internet Card')
        return 1
    end
    if type(config.name) ~= 'string' or config.name == '' then
        log.err('config.lua: name the setup')
        return 1
    end

    local m = miner.new({
        -- OpenOS's component.list table is also its own iterator; the gtnh stub knows the table only.
        ---@diagnostic disable-next-line: return-type-mismatch
        list = function(kind) return component.list(kind, true) end,
        proxy = component.proxy,
        post = function(path, body) return http.post(config.server .. path, body) end,
        decode = serialization.unserialize,
        config = config,
        run = ('%s-%d'):format(computer.address():sub(1, 8), math.floor(os.time())),
        warn = log.wrn,
        info = log.inf,
        logs = logs,
    }, computer.uptime())
    log.inf('mining agent %s for %s; Ctrl+C to stop', config.name, config.server)

    repeat
        -- A bug in a step must not kill the agent; the traceback says where it was.
        local ok, wait = xpcall(m.step, debug.traceback, m, computer.uptime())
        if not ok then
            log.err('step failed: %s', tostring(wait))
            wait = kRetry
        end
    until event.pull(wait, 'interrupted') == 'interrupted'
    m:stop(computer.uptime())
    log.inf('stopped; the modules end their operation and stand')
    return 0
end

return main()
