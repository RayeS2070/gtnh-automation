---Checks the cheap point lookup factoryctl wants to use for target stock against the full
---filtered list, and times both. getItemInNetwork is a hash lookup; getItemsInNetwork(filter)
---converts every stack of the network first (slow, on the game server's thread).
---  stock-probe <name> [damage] [--addr=<prefix>]          an item, e.g. minecraft:stick 0
---  stock-probe --fluid=<fluid name> [--addr=<prefix>]     a fluid drop, e.g. --fluid=water
local component = require('component')
local computer = require('computer')
local shell = require('shell')

local args, opts = shell.parse(...)

local function findProxy()
    for _, ctype in ipairs({ 'me_interface', 'me_controller' }) do
        for address in component.list(ctype, true) do
            if type(opts.addr) ~= 'string' or address:sub(1, #opts.addr) == opts.addr then
                return component.proxy(address)
            end
        end
    end
    error('no me_interface or me_controller')
end

local function timed(fn, ...)
    local t = computer.uptime()
    local ok, a = pcall(fn, ...)
    return ok, a, computer.uptime() - t
end

local me = findProxy()
local name, damage, nbt, filter
if type(opts.fluid) == 'string' then
    name, damage = 'ae2fc:fluid_drop', 0
    nbt = ('{Fluid:"%s"}'):format(opts.fluid)
    filter = { name = name }
else
    name, damage = args[1], tonumber(args[2]) or 0
    if not name then
        print('usage: stock-probe <name> [damage] | --fluid=<fluid name>')
        return
    end
    filter = { name = name, damage = damage }
end

print(('point:  getItemInNetwork(%q, %d%s)'):format(name, damage, nbt and (', ' .. nbt) or ''))
local ok, stack, dt = timed(me.getItemInNetwork, name, damage, nbt)
if not ok then
    print('  error: ' .. tostring(stack))
elseif stack == nil then
    print(('  nil (not found)   %.2fs'):format(dt))
else
    print(('  size %s  label %s  craftable %s   %.2fs'):format(tostring(stack.size), tostring(stack.label),
        tostring(stack.isCraftable), dt))
end

print('full:   getItemsInNetwork(filter)')
local okAll, list, dtAll = timed(me.getItemsInNetwork, filter)
if not okAll then
    print('  error: ' .. tostring(list))
    return
end
local total, n = 0, 0
for _, s in ipairs(list) do
    local fits = not opts.fluid or (s.fluidDrop and s.fluidDrop.name == opts.fluid)
    if fits then
        n = n + 1
        total = total + (s.size or 0)
        print(('  size %s  label %s  hasTag %s'):format(tostring(s.size), tostring(s.label), tostring(s.hasTag)))
    end
end
print(('  %d stacks, total %d   %.2fs'):format(n, total, dtAll))
