---What OC sees around a Pseudostable Black Hole Containment Field, for setting up the black hole
---agent. Changes nothing (no transfers, no setWorkAllowed).
---  bhc-dump                 components, stocking hatches, sides of the transposer and Redstone
---                           I/O, supplies lookups
---  bhc-dump --watch=60      then for 60 s: every redstone change and the transposer's tanks
---                           once a second (compare with the stability in WAILA)
---  bhc-dump --out=/home/bhc-dump.txt   also write everything to a file
local component = require('component')
local computer = require('computer')
local event = require('event')
local shell = require('shell')

local _, options = shell.parse(...)
local out = type(options.out) == 'string' and io.open(options.out, 'w') or nil

local kSideNames = { [0] = 'bottom', 'top', 'north', 'south', 'west', 'east' }
local kSeed, kCollapser = 32418, 32419

local function say(fmt, ...)
    local line = fmt:format(...)
    print(line)
    if out then
        out:write(line, '\n')
    end
end

local function try(f, ...)
    local ok, a, b = pcall(f, ...)
    if not ok then
        return nil, tostring(a)
    end
    return a, b
end

local function show(value)
    if type(value) ~= 'table' then
        return tostring(value)
    end
    local parts = {}
    for k, v in pairs(value) do
        if type(v) ~= 'table' then
            parts[#parts + 1] = ('%s=%s'):format(tostring(k), tostring(v))
        end
    end
    table.sort(parts)
    return '{' .. table.concat(parts, ', ') .. '}'
end

say('== gt_machine')
for address in component.list('gt_machine', true) do
    local p = component.proxy(address)
    local name = try(p.getName)
    say('%s  %s', address, tostring(name))
    if type(name) == 'string' and name:find('blackhole', 1, true) then
        say('  work allowed %s, active %s, progress %s / %s', tostring(try(p.isWorkAllowed)),
            tostring(try(p.isMachineActive)), tostring(try(p.getWorkProgress)),
            tostring(try(p.getWorkMaxProgress)))
        local info = try(p.getSensorInformation) or {}
        for _, line in ipairs(info) do
            say('  | %s', tostring(line))
        end
    end
end

say('== redstone')
for address in component.list('redstone', true) do
    local p = component.proxy(address)
    local inputs = {}
    for side = 0, 5 do
        inputs[#inputs + 1] = ('%d %s=%s'):format(side, kSideNames[side],
            tostring(try(p.getInput, side)))
    end
    say('%s  inputs: %s', address, table.concat(inputs, ', '))
end

say('== me_inputhatch (the spacetime hatch is one of them: auto-pull off, slots empty)')
for address in component.list('me_inputhatch', true) do
    local p = component.proxy(address)
    local slots = {}
    for slot = 1, try(p.getSlotSize) or 16 do
        local fluid = try(p.getConfiguration, slot)
        if type(fluid) == 'table' and fluid.name then
            slots[#slots + 1] = ('%d=%s'):format(slot, tostring(fluid.name))
        end
    end
    say('%s  auto-pull %s, slots: %s', address, tostring(try(p.getAutoPull)),
        #slots > 0 and table.concat(slots, ' ') or 'empty')
end

local transposers = {}
say('== transposer')
for address in component.list('transposer', true) do
    local p = component.proxy(address)
    transposers[#transposers + 1] = p
    say('%s', address)
    for side = 0, 5 do
        local name = try(p.getInventoryName, side)
        local size = try(p.getInventorySize, side)
        local tanks = try(p.getTankCount, side)
        if name or (tanks and tanks > 0) then
            say('  side %d %s: %s, %s slots, %s tanks', side, kSideNames[side], tostring(name),
                tostring(size), tostring(tanks))
            for slot = 1, math.min(size or 0, 27) do
                local stack = try(p.getStackInSlot, side, slot)
                if stack then
                    say('    slot %d: %s:%s x%s %s', slot, tostring(stack.name),
                        tostring(stack.damage), tostring(stack.size), tostring(stack.label))
                end
            end
            for tank = 1, math.min(tanks or 0, 8) do
                local fluid = try(p.getFluidInTank, side, tank)
                say('    tank %d: level %s, %s', tank, tostring(try(p.getTankLevel, side, tank)),
                    show(fluid))
            end
        end
    end
end

say('== ME (supplies lookups: seed %d, collapser %d, molten.spacetime)', kSeed, kCollapser)
for _, kind in ipairs({ 'fluid_interface', 'me_controller', 'me_interface' }) do
    for address in component.list(kind, true) do
        local p = component.proxy(address)
        local items = try(p.getItemsInNetwork) or {}
        local fluids = p.getFluidsInNetwork and try(p.getFluidsInNetwork) or {}
        say('%s  %s: %d item stacks, %d fluids', address, kind, #items, #fluids)
        if p.getItemInNetwork then
            say('  seed: %s', show(try(p.getItemInNetwork, 'gregtech:gt.metaitem.01', kSeed)))
            say('  collapser: %s', show(try(p.getItemInNetwork, 'gregtech:gt.metaitem.01',
                kCollapser)))
        end
        if p.getFluidInNetwork then
            say('  spacetime: %s', show(try(p.getFluidInNetwork, 'molten.spacetime')))
        end
        for i = 1, math.min(#fluids, 10) do
            say('  fluid %s %s x%s', tostring(fluids[i].name), tostring(fluids[i].label),
                tostring(fluids[i].amount))
        end
    end
end

local watch = tonumber(options.watch)
if watch and watch > 0 then
    say('== watch %d s: redstone changes and tanks once a second (Ctrl+C stops)', watch)
    local stop = computer.uptime() + watch
    local nextTanks = 0
    while computer.uptime() < stop do
        local name, a, b, c, d = event.pull(0.1)
        local now = computer.uptime()
        if name == 'interrupted' then
            break
        elseif name == 'redstone_changed' then
            say('%9.2f redstone %s side %s: %s -> %s', now, tostring(a):sub(1, 8), tostring(b),
                tostring(c), tostring(d))
        end
        if now >= nextTanks then
            nextTanks = now + 1
            for _, p in ipairs(transposers) do
                local levels = {}
                for side = 0, 5 do
                    local level = try(p.getTankLevel, side)
                    if level and level > 0 then
                        levels[#levels + 1] = ('%d=%s'):format(side, tostring(level))
                    end
                end
                say('%9.2f tanks %s: %s', now, p.address:sub(1, 8), table.concat(levels, ' '))
            end
        end
    end
end

if out then
    out:close()
    print('written to ' .. options.out)
end
