---The real agent against a running server, outside the game: HTTP through curl, the ME network
---faked from the dumps. Checks that both ends speak the same protocol. Not installed in game.
---  cd agent && lua tests/live.lua [http://localhost:8080] [polls]
package.path = './?.lua;' .. package.path
local agent = require('fc.agent')
local server, polls = arg[1] or 'http://localhost:8080', tonumber(arg[2]) or 3

local function post(path, body)
    local tmp = os.tmpname()
    local f = assert(io.open(tmp, 'w'))
    f:write(body)
    f:close()
    local p = io.popen(('curl -sS -X POST -H "Content-Type: application/json" --data-binary @%s %s%s'):format(
        tmp, server, path))
    local text = p:read('a')
    local ok = p:close()
    os.remove(tmp)
    if not ok then return nil, 'curl failed' end
    return text
end

-- The network of the dump, its CPUs, and craftables that accept every order.
local stacks = dofile('tests/fixtures/me_sample.lua')
local cpus = dofile('tests/fixtures/cpus.lua')
local me = {}
function me.getItemsInNetworkById(ids)
    local out = {}
    for _, s in ipairs(stacks) do
        if s._chunk >= ids[1] and s._chunk <= ids[#ids] then out[#out + 1] = s end
    end
    return out
end
function me.getCpus()
    local out = {}
    for _, r in ipairs(cpus) do
        out[#out + 1] = { name = r.name, busy = r.busy,
            cpu = { finalOutput = function() return r.finalOutput end, cancel = function() return true end } }
    end
    return out
end
function me.getItemInNetwork(name, damage, nbt)
    local fluid = nbt and nbt:match('^{Fluid:"(.*)"}$')
    for _, st in ipairs(stacks) do
        if st.name == name and (fluid and st.fluidDrop and st.fluidDrop.name == fluid
                or not fluid and st.damage == damage) then
            return st
        end
    end
    return nil
end
function me.getCraftables(filter)
    print(('  getCraftables{name=%s, damage=%s, label=%s}'):format(filter.name, tostring(filter.damage),
        tostring(filter.label)))
    return { { request = function(amount)
        print('  request ' .. amount)
        return { isComputing = function() return false end, hasFailed = function() return false end,
                 isCanceled = function() return false end, isDone = function() return false end }
    end } }
end

local function say(fmt, ...) print(fmt:format(...)) end
local a = agent.new({ me = me, post = post, decode = function(t) return load('return ' .. t, '=r', 't', {})() end,
    warn = function(fmt, ...) say('WRN ' .. fmt, ...) end, info = function(fmt, ...) say('INF ' .. fmt, ...) end })
for i = 1, polls do
    local reply, err = a:poll()
    say('poll %d: %s', i, reply and ('ok, poll=' .. tostring(reply.poll)) or ('ERROR ' .. tostring(err)))
end
