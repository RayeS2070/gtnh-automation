---Dumps what a mining agent for the Space Elevator needs to know, into one file of Lua tables:
---  * every component: type and address (what this computer reaches);
---  * every gt_machine: getName(); for the mining modules (projectmoduleminert1..3) and the Space
---    Elevator itself, all their methods and what the harmless getters give: parameters, sensor
---    information, work state, coordinates;
---  * each ME stocking input bus and hatch: its settings and every configured slot;
---  * each transposer: what every side holds (a drone bus, an interface buffer);
---  * the ME networks: drones (getItemInNetwork per tier: a hash lookup), the steel drill tip and
---    rod (getItemsInNetwork with a label filter: it still converts the whole network once per
---    call, fine for one dump), plasmas. No unfiltered item scan: one took the game server down.
---Changes nothing. From the factoryctl directory (/home/factoryctl):
---  mining/tools/mining-dump.lua [file]      (default /home/mining-dump.txt)
local component = require('component')
local shell = require('shell')

local args = shell.parse(...)
local path = args[1] or '/home/mining-dump.txt'

local kDrone = 'gtnhintergalactic:item.MiningDrone'
local kDroneTiers = 14 -- damage 0 (LV) .. 13 (MAX)
-- Labels of the drill consumables of the lowest tier, to see how they look in the network.
local kLabels = { 'Steel Drill Tip', 'Steel Rod' }
local kSlots = 16 -- slots looked at per transposer side and stocking bus

---Calls a method if the proxy has it: the value, or a string saying why there is none.
local function ask(proxy, method, ...)
    if not proxy[method] then
        return '<no method>'
    end
    local ok, a, b = pcall(proxy[method], ...)
    if not ok then
        return '<error: ' .. tostring(a) .. '>'
    end
    if a == nil and b ~= nil then
        return '<nil: ' .. tostring(b) .. '>'
    end
    return a
end

---All the values a method returns (getCoordinates gives x, y, z), or why there are none.
local function askAll(proxy, method, ...)
    if not proxy[method] then
        return '<no method>'
    end
    local r = table.pack(pcall(proxy[method], ...))
    if not r[1] then
        return '<error: ' .. tostring(r[2]) .. '>'
    end
    return table.pack(table.unpack(r, 2, r.n))
end

local function methods(address)
    local out = {}
    for name, direct in pairs(component.methods(address)) do
        out[#out + 1] = name .. (direct and '' or ' (indirect)')
    end
    table.sort(out)
    return out
end

local function isMiner(name)
    return type(name) == 'string' and name:lower():find('projectmodulemine', 1, true) ~= nil
end

local function isElevator(name)
    return type(name) == 'string' and name:lower():find('elevator', 1, true) ~= nil
end

---Stacks as a list without the heavy fields (tag: binary NBT).
local function stacks(list)
    if type(list) ~= 'table' then
        return list
    end
    local out = {}
    for i, s in ipairs(list) do
        if type(s) == 'table' then
            out[i] = { name = s.name, damage = s.damage, label = s.label, size = s.size,
                amount = s.amount, isCraftable = s.isCraftable, hasTag = s.hasTag }
        end
    end
    return out
end

local dump = { components = {}, machines = {}, modules = {}, stocking = {}, transposers = {},
    networks = {} }

for address, kind in component.list() do
    dump.components[#dump.components + 1] = { type = kind, address = address }
end
table.sort(dump.components, function(a, b)
    if a.type ~= b.type then return a.type < b.type end
    return a.address < b.address
end)

for address in component.list('gt_machine', true) do
    local proxy = component.proxy(address)
    local name = ask(proxy, 'getName')
    dump.machines[#dump.machines + 1] = { address = address, name = name }
    if isMiner(name) or isElevator(name) then
        dump.modules[#dump.modules + 1] = {
            address = address,
            name = name,
            methods = methods(address),
            coordinates = askAll(proxy, 'getCoordinates'),
            workAllowed = ask(proxy, 'isWorkAllowed'),
            active = ask(proxy, 'isMachineActive'),
            hasWork = ask(proxy, 'hasWork'),
            progress = ask(proxy, 'getWorkProgress'),
            maxProgress = ask(proxy, 'getWorkMaxProgress'),
            parameters = ask(proxy, 'getParameters'),
            sensor = ask(proxy, 'getSensorInformation'),
        }
    end
end

for _, kind in ipairs({ 'me_inputbus', 'me_inputhatch' }) do
    for address in component.list(kind, true) do
        local proxy = component.proxy(address)
        local size = ask(proxy, 'getSlotSize')
        local slots = {}
        for slot = 1, type(size) == 'number' and size or kSlots do
            local c = ask(proxy, 'getConfiguration', slot)
            if (type(c) == 'table' and next(c) ~= nil) or type(c) == 'string' then
                slots[#slots + 1] = { slot = slot, configuration = stacks({ c })[1] or c }
            end
        end
        dump.stocking[#dump.stocking + 1] = {
            type = kind,
            address = address,
            name = ask(proxy, 'getName'),
            advanced = ask(proxy, 'isAdvanced'),
            coordinates = askAll(proxy, 'getCoordinates'),
            slotSize = size,
            autoPull = ask(proxy, 'getAutoPull'),
            workAllowed = ask(proxy, 'isWorkAllowed'),
            slots = slots,
        }
    end
end

for address in component.list('transposer', true) do
    local proxy = component.proxy(address)
    local sides = {}
    for side = 0, 5 do
        local size = ask(proxy, 'getInventorySize', side)
        if type(size) == 'number' then
            local held = {}
            for slot = 1, math.min(size, kSlots) do
                local s = ask(proxy, 'getStackInSlot', side, slot)
                if type(s) == 'table' then
                    held[#held + 1] = { slot = slot, stack = stacks({ s })[1] }
                end
            end
            sides[#sides + 1] = { side = side, inventory = ask(proxy, 'getInventoryName', side),
                size = size, stacks = held }
        end
    end
    dump.transposers[#dump.transposers + 1] = { address = address, sides = sides }
end

for _, kind in ipairs({ 'fluid_interface', 'me_controller', 'me_interface' }) do
    for address in component.list(kind, true) do
        local proxy = component.proxy(address)
        local net = { type = kind, address = address, drones = {}, drill = {} }
        for damage = 0, kDroneTiers - 1 do
            local d = ask(proxy, 'getItemInNetwork', kDrone, damage)
            if type(d) == 'table' then
                net.drones[#net.drones + 1] = stacks({ d })[1]
            elseif type(d) == 'string' and damage == 0 then
                net.drones = d -- no method or an error: once is enough
                break
            end
        end
        for _, label in ipairs(kLabels) do
            net.drill[label] = stacks(ask(proxy, 'getItemsInNetwork', { label = label }))
        end
        -- Plasmas: the fluids whose name says so; the whole fluid list is small.
        local fluids = ask(proxy, 'getFluidsInNetwork')
        if type(fluids) == 'table' then
            local plasmas = {}
            for _, f in ipairs(fluids) do
                if type(f) == 'table' and type(f.name) == 'string' and f.name:find('plasma', 1, true) then
                    plasmas[#plasmas + 1] = f
                end
            end
            net.plasmas = stacks(plasmas)
            net.fluidCount = #fluids
        else
            net.plasmas = fluids
        end
        dump.networks[#dump.networks + 1] = net
    end
end

local f, err = io.open(path, 'w')
if not f then
    io.stderr:write(('cannot write %s: %s\n'):format(path, tostring(err)))
    return 1
end
---Every field, nothing cut: OpenOS's serialization.serialize(v, true) stops a table after ten
---lines.
local function write(v, indent)
    if type(v) == 'string' then
        f:write(('%q'):format(v))
    elseif type(v) ~= 'table' then
        f:write(tostring(v))
    else
        local keys = {}
        for k in pairs(v) do keys[#keys + 1] = k end
        table.sort(keys, function(a, b)
            if type(a) == type(b) then return a < b end
            return type(a) == 'number'
        end)
        f:write('{\n')
        for _, k in ipairs(keys) do
            f:write(indent, '  ')
            if type(k) ~= 'number' then
                f:write(tostring(k), ' = ')
            end
            write(v[k], indent .. '  ')
            f:write(',\n')
        end
        f:write(indent, '}')
    end
end

local kSections = { 'components', 'machines', 'modules', 'stocking', 'transposers', 'networks' }
for _, section in ipairs(kSections) do
    f:write('-- ' .. section .. '\n')
    write(dump[section], '')
    f:write('\n\n')
end
f:close()

local miners = 0
for _, m in ipairs(dump.modules) do
    if isMiner(m.name) then miners = miners + 1 end
    print(('  %s %s'):format(m.address:sub(1, 8), tostring(m.name)))
end
print(('%d components, %d gt_machine, %d mining modules, %d stocking, %d transposers, %d networks -> %s')
    :format(#dump.components, #dump.machines, miners, #dump.stocking, #dump.transposers,
        #dump.networks, path))
if miners == 0 then
    print('No mining module found: put an Adapter on a module controller and run it again.')
end
return 0
