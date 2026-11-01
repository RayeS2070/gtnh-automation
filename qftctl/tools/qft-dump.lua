---Dumps what the move of qftctl to GTNH 2.9 needs to know, into one file of Lua tables:
---  * every component of interest: type, address, getName();
---  * each ME (advanced) stocking input bus and hatch: its settings and the configuration of every
---    slot, as getConfiguration() returns it (configure one or two slots by hand first);
---  * the QFT controller: parameters, sensor information, work progress;
---  * a few fluids and items of each fluid interface / ME controller, as 2.9 gives them.
---Changes nothing. Run from the qftctl directory:
---  tools/qft-dump.lua [file]      (default /home/qft-dump.txt)
local component = require('component')
local serialization = require('serialization')
local shell = require('shell')

local args = shell.parse(...)
local path = args[1] or '/home/qft-dump.txt'

local kTypes = { 'me_inputbus', 'me_inputhatch', 'gt_machine', 'fluid_interface', 'me_controller',
    'me_interface', 'me_storagebus', 'fluid_storagebus' }
local kSample = 3 -- fluids and items shown per network

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

local function first(list, n)
    local out = {}
    if type(list) ~= 'table' then
        return list
    end
    for i = 1, math.min(n, #list) do
        out[i] = list[i]
    end
    return out
end

local dump = { components = {}, stocking = {}, qft = {}, networks = {} }

for _, kind in ipairs(kTypes) do
    for address in component.list(kind, true) do
        local proxy = component.proxy(address)
        dump.components[#dump.components + 1] = { type = kind, address = address,
            name = proxy.getName and ask(proxy, 'getName') or nil }

        if kind == 'me_inputbus' or kind == 'me_inputhatch' then
            local size = ask(proxy, 'getSlotSize')
            local slots = {}
            -- Whether slots count from 0 or 1 is not known: both ends are asked.
            local last = type(size) == 'number' and size or 16
            for slot = 0, last do
                slots[#slots + 1] = { slot = slot, configuration = ask(proxy, 'getConfiguration', slot) }
            end
            dump.stocking[#dump.stocking + 1] = {
                type = kind,
                address = address,
                name = ask(proxy, 'getName'),
                advanced = ask(proxy, 'isAdvanced'),
                slotSize = size,
                autoPull = ask(proxy, 'getAutoPull'),
                minAmount = ask(proxy, 'getMinAmount'),
                refreshTime = ask(proxy, 'getRefreshTime'),
                workAllowed = ask(proxy, 'isWorkAllowed'),
                circuit = kind == 'me_inputbus' and ask(proxy, 'getCircuitConfiguration') or nil,
                slots = slots,
            }
        elseif kind == 'gt_machine' then
            local name = ask(proxy, 'getName')
            if type(name) == 'string' and name:find('quantumforcetransformer', 1, true) then
                dump.qft[#dump.qft + 1] = {
                    address = address,
                    name = name,
                    workAllowed = ask(proxy, 'isWorkAllowed'),
                    active = ask(proxy, 'isMachineActive'),
                    progress = ask(proxy, 'getWorkProgress'),
                    maxProgress = ask(proxy, 'getWorkMaxProgress'),
                    parameters = ask(proxy, 'getParameters'),
                    sensor = ask(proxy, 'getSensorInformation'),
                }
            end
        elseif kind == 'fluid_interface' or kind == 'me_controller' or kind == 'me_interface' then
            dump.networks[#dump.networks + 1] = {
                type = kind,
                address = address,
                fluids = first(ask(proxy, 'getFluidsInNetwork'), kSample),
                items = first(ask(proxy, 'getItemsInNetwork'), kSample),
            }
        end
    end
end

local f, err = io.open(path, 'w')
if not f then
    io.stderr:write(('cannot write %s: %s\n'):format(path, tostring(err)))
    return 1
end
for _, section in ipairs({ 'components', 'stocking', 'qft', 'networks' }) do
    f:write('-- ' .. section .. '\n')
    f:write(serialization.serialize(dump[section], true), '\n\n')
end
f:close()

print(('%d components, %d stocking buses/hatches, %d QFT controllers, %d networks -> %s'):format(
    #dump.components, #dump.stocking, #dump.qft, #dump.networks, path))
for _, s in ipairs(dump.stocking) do
    local configured = 0
    for _, slot in ipairs(s.slots) do
        if type(slot.configuration) == 'table' and next(slot.configuration) then
            configured = configured + 1
        end
    end
    print(('  %s %s: %s slots, %d configured'):format(s.type, s.address:sub(1, 8),
        tostring(s.slotSize), configured))
end
if #dump.stocking > 0 then
    local any = false
    for _, s in ipairs(dump.stocking) do
        for _, slot in ipairs(s.slots) do
            any = any or (type(slot.configuration) == 'table' and next(slot.configuration) ~= nil)
        end
    end
    if not any then
        print('No slot is configured: set one or two by hand (an item, a fluid) and run it again.')
    end
end
return 0
