---factoryctl agent: runs crafting orders of the factoryctl server and scans the ME network for
---it. Needs an Internet Card and an Adapter next to an ME interface. Ctrl+C stops it.
---  main
-- OpenOS keeps required modules in package.loaded until reboot: drop ours so an update runs.
for name in pairs(package.loaded) do
    if name:match('^fc%.') or name == 'config' then
        package.loaded[name] = nil
    end
end

local component = require('component')
local computer = require('computer')
local event = require('event')
local serialization = require('serialization')
local agent = require('fc.agent')
local http = require('fc.http')
local log = require('fc.log')
local logbuf = require('fc.logbuf')
local trigger = require('fc.trigger')
local config = require('config')

local kRetry = 10 -- seconds before polling again after a failed poll

local function findInterface(prefix)
    local found = {}
    for _, kind in ipairs({ 'me_interface', 'me_controller' }) do
        for address in component.list(kind, true) do
            if not prefix or address:sub(1, #prefix) == prefix then
                found[#found + 1] = address
            end
        end
    end
    if #found == 0 then
        return nil, 'no me_interface or me_controller' .. (prefix and (' at ' .. prefix) or '')
    end
    if #found > 1 and not prefix then
        return nil, ('%d ME components: set `interface` in config.lua'):format(#found)
    end
    return component.proxy(found[1])
end

local function main()
    log.clock = computer.uptime
    local levels = { dbg = log.Level.Dbg, inf = log.Level.Inf, wrn = log.Level.Wrn,
        err = log.Level.Err }
    log.setLevel(levels[config.logLevel] or log.Level.Inf)
    -- The log also goes to the server, with the polls: there it is kept next to its own.
    local logs = logbuf.new()
    log.setForwardLevel(levels[config.forwardLevel] or log.Level.Inf)
    log.forward = function(level, text) logs:push(computer.uptime(), level, text) end
    if not component.isAvailable('internet') then
        log.err('no Internet Card')
        return 1
    end
    local proxy, err = findInterface(config.interface)
    if not proxy then
        log.err('%s', err)
        return 1
    end

    local a = agent.new({
        me = proxy,
        post = function(path, body) return http.post(config.server .. path, body) end,
        decode = serialization.unserialize,
        yield = function() os.sleep(0) end,
        warn = log.wrn,
        info = log.inf,
        logs = logs,
    })
    log.inf('agent for %s, ME %s; Ctrl+C to stop', config.server, proxy.address:sub(1, 8))

    -- 2.8.4 has no events from AE: look at our own jobs every second and poll at once when one
    -- ends. On 2.9 an event-driven trigger replaces this one (see fc/trigger.lua).
    local waiter = trigger.polling({
        jobs = a.jobs,
        pull = function(timeout) return event.pull(timeout, 'interrupted') end,
        time = computer.uptime,
    })

    local wait, why = 0, nil
    repeat
        -- A bug in a poll must not kill the agent; the traceback says where it was.
        local ok, reply, perr = xpcall(a.poll, debug.traceback, a)
        if not ok then
            log.err('poll failed: %s', tostring(reply))
            wait = kRetry
        elseif not reply then
            log.wrn('poll: %s', tostring(perr))
            wait = kRetry
        else
            log.dbg('poll: %d orders, %d cancels%s', #(reply.orders or {}), #(reply.cancel or {}),
                reply.scan and ', scan' or '')
            wait = reply.poll or kRetry
        end
        why = waiter:wait(wait)
        if why == 'early' then
            log.dbg('a craft ended: polling early')
        end
    until why == 'interrupted'
    log.inf('stopped; crafts already requested go on in AE')
    return 0
end

return main()
