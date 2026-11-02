---The QFT agent's link against a running server, outside the game: HTTP through curl, a small
---recipe set, a made-up report. Checks that both ends speak the same protocol. Not installed.
---  cd qft && lua tests/live.lua [http://localhost:8081] [name] [x]
package.path = './?.lua;../agent/?.lua;' .. package.path
local config = require('qft.config')
local link = require('qft.link')
local plan = require('qft.plan')
local resource = require('qft.resource')
local server, name, x = arg[1] or 'http://localhost:8081', arg[2] or 'qft-live', tonumber(arg[3]) or 1

local function post(path, body)
    local tmp = os.tmpname()
    local f = assert(io.open(tmp, 'w'))
    f:write(body)
    f:close()
    local p = io.popen(('curl -sS -X POST -H "Content-Type: application/json" --data-binary @%s %s%s')
        :format(tmp, server, path))
    local text = p:read('a')
    p:close()
    os.remove(tmp)
    print('  <- ' .. text:sub(1, 200))
    return text
end

local recipes = {
    glue = { name = 'Glue', trigger = 'deficit', tier = 1, catalysts = 4,
        inputs = { { fluid = 'Water' } },
        outputs = { { fluid = 'Glue', circuit = 1, target = 1000, priority = 2 }, { fluid = 'Waste', circuit = 2 } } },
    plat = { name = 'Platinum Group', trigger = 'surplus', tier = 1, catalysts = 1,
        inputs = { { item = 'Ore', amount = 32 } },
        outputs = { { fluid = 'Molten Platinum', circuit = 1, weight = 2 }, { fluid = 'Molten Palladium', circuit = 2, weight = 1 } } },
}
assert(#config.normalize(recipes) == 0)
local saved
local l = link.new({
    post = post,
    decode = function(text) return load('return ' .. text, '=reply', 't', {})() end,
    store = { read = function() return saved end, write = function(t) saved = t; return true end,
        encode = function() return '{}' end, decode = function() return {} end },
    recipes = recipes, mode = 'auto', name = name, coords = { x = x, y = 64, z = -10 },
    run = 'live-' .. os.time(),
    warn = function(fmt, ...) print('WRN ' .. fmt:format(...)) end,
    info = function(fmt, ...) print('INF ' .. fmt:format(...)) end,
})
local function snap(t)
    local out = {}
    for label, amount in pairs(t) do out[#out + 1] = { label = label, name = label:lower(), amount = amount } end
    return resource.snapshot(nil, out)
end
local states = plan.evaluate(recipes, snap({ Glue = 300 }), resource.snapshot({ { label = 'Ore', name = 'ore', damage = 0, size = 640 } }, {}))
for i = 1, tonumber(arg[4]) or 2 do
    local reply, err = l:poll({ phase = 'running', mode = l.settings.mode, want = 'glue',
        running = { recipe = 'glue', fermium = i > 1, neptunium = 1 }, cycleSeconds = 6,
        states = states, plasma = { neptunium = 5000, fermium = 9000 } })
    print(reply and ('poll %d ok%s'):format(i, reply.changed and ', settings changed' or '') or ('poll failed: ' .. tostring(err)))
end
print(('settings v%d mode %s glue target %s'):format(l.settings.version, l.settings.mode,
    tostring(recipes.glue.outputs[1].target)))
