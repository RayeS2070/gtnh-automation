---factoryctl pumping agent: runs the Space Elevator pump modules to keep fluids of the pumping
---ME network at their targets, set from the factoryctl server and kept without it. Needs an
---Internet Card, Adapters at the pump modules and at the pumping network's fluid interface or ME
---controller. Ctrl+C stops it.
---  main
-- OpenOS keeps required modules in package.loaded until reboot: drop ours so an update runs.
for name in pairs(package.loaded) do
    if name:match('^fc%.') or name:match('^fp%.') or name == 'config' then
        package.loaded[name] = nil
    end
end

local shell = require('shell')
-- This program's directory: fp/ and config.lua are here, the shared fc/ in ../agent.
local here = (debug.getinfo(1, 'S').source:match('^[@=](/.*)/[^/]*$'))
    or shell.getWorkingDirectory()
package.path = ('%s/?.lua;%s/../agent/?.lua;%s'):format(here, here, package.path)

local component = require('component')
local computer = require('computer')
local event = require('event')
local filesystem = require('filesystem')
local serialization = require('serialization')
local http = require('fc.http')
local log = require('fc.log')
local logbuf = require('fc.logbuf')
local network = require('fc.network')
local pumps = require('fp.pumps')
local config = require('config')

local kRetry = 10 -- s after a step that threw
local kSaved = here .. '/settings.dat'

local store = {
    read = function()
        local f = io.open(kSaved, 'r')
        if not f then
            return nil
        end
        local text = f:read('*a')
        f:close()
        return text
    end,
    -- A new file first: a write cut short (a reboot) must not leave the old one broken.
    write = function(text)
        local tmp = kSaved .. '.new'
        local f, err = io.open(tmp, 'w')
        if not f then
            return nil, err
        end
        f:write(text)
        f:close()
        filesystem.remove(kSaved)
        return filesystem.rename(tmp, kSaved)
    end,
    encode = serialization.serialize,
    decode = serialization.unserialize,
}

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
    local address, kind = network.find(function(k) return component.list(k, true) end, config.me)
    if not address then
        log.err('%s', kind)
        return 1
    end
    local me = component.proxy(address)

    local p, perr = pumps.new({
        list = function() return component.list('gt_machine', true) end,
        proxy = component.proxy,
        fluids = me.getFluidsInNetwork,
        post = function(path, body) return http.post(config.server .. path, body) end,
        decode = serialization.unserialize,
        store = store,
        defaults = config.defaults,
        run = ('%s-%d'):format(computer.address():sub(1, 8), math.floor(os.time())),
        warn = log.wrn,
        info = log.inf,
        logs = logs,
    }, computer.uptime())
    if not p then
        log.err('%s', perr)
        return 1
    end
    log.inf('pumping agent for %s, %s %s; Ctrl+C to stop', config.server, kind, address:sub(1, 8))

    repeat
        -- A bug in a step must not kill the agent; the traceback says where it was.
        local ok, wait = xpcall(p.step, debug.traceback, p, computer.uptime(), computer.uptime)
        if not ok then
            log.err('step failed: %s', tostring(wait))
            wait = kRetry
        end
    until event.pull(wait, 'interrupted') == 'interrupted'
    p:stop()
    log.inf('stopped; cycles already running end by themselves')
    return 0
end

return main()
