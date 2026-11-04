---Dumps what OpenComputers sees of a Nanochip Assembly Complex, into one file of Lua tables:
---  * every component type on this computer and how many there are;
---  * every gt_machine: address and getName(), so the NAC parts can be told from the rest;
---  * each NAC part (the controller, modules, vacuum conveyor hatches, the splitter redstone
---    hatch; getName() has "nanochip", "vacuum.hatch" or "hatch.nanochip"): its methods with
---    their docs, and the value of every getter that takes no arguments (get*, is*, has*).
---Calls only getters: changes nothing. Put an adapter next to the controller, a module or a
---hatch (several adapters: all of them at once) and run:
---  /home/factoryctl/nac/tools/nac-dump.lua [file]      (default /home/nac-dump.txt)
local component = require('component')
local shell = require('shell')

local args = shell.parse(...)
local path = args[1] or '/home/nac-dump.txt'

local kParts = { 'nanochip', 'vacuum.hatch', 'hatch.nanochip' }

local function isPart(name)
    if type(name) ~= 'string' then
        return false
    end
    for _, p in ipairs(kParts) do
        if name:find(p, 1, true) then
            return true
        end
    end
    return false
end

---Calls a getter: its values, or a string saying why there are none.
local function ask(proxy, method)
    local result = table.pack(pcall(proxy[method]))
    if not result[1] then
        return '<error: ' .. tostring(result[2]) .. '>'
    end
    if result.n <= 2 then
        return result[2]
    end
    return { table.unpack(result, 2, result.n) }
end

local dump = { types = {}, machines = {}, parts = {} }

local counts = {}
for _, kind in component.list() do
    counts[kind] = (counts[kind] or 0) + 1
end
for kind, n in pairs(counts) do
    dump.types[kind] = n
end

for address in component.list('gt_machine', true) do
    local proxy = component.proxy(address)
    local name = proxy.getName and ask(proxy, 'getName') or '<no getName>'
    dump.machines[#dump.machines + 1] = { address = address, name = name }
    if isPart(name) then
        local methods, values = {}, {}
        for method in pairs(proxy) do
            if type(proxy[method]) == 'table' or type(proxy[method]) == 'function' then
                local doc = component.doc(address, method)
                if doc then
                    methods[method] = doc
                    if method:match('^get') or method:match('^is') or method:match('^has') then
                        values[method] = ask(proxy, method)
                    end
                end
            end
        end
        dump.parts[#dump.parts + 1] = { address = address, name = name, methods = methods,
            values = values }
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
                f:write(('[%q] = '):format(tostring(k)))
            end
            write(v[k], indent .. '  ')
            f:write(',\n')
        end
        f:write(indent, '}')
    end
end

for _, section in ipairs({ 'types', 'machines', 'parts' }) do
    f:write('-- ' .. section .. '\n')
    write(dump[section], '')
    f:write('\n\n')
end
f:close()

print(('%d gt_machine, %d of them NAC parts -> %s'):format(#dump.machines, #dump.parts, path))
for _, p in ipairs(dump.parts) do
    local n = 0
    for _ in pairs(p.methods) do n = n + 1 end
    print(('  %s %s: %d methods'):format(p.address:sub(1, 8), tostring(p.name), n))
end
if #dump.parts == 0 then
    print('No NAC part seen: an adapter must touch the controller, a module or a hatch.')
end
return 0
