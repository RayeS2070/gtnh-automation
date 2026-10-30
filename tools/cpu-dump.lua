---Dumps the crafting CPUs of an ME network as getCpus() returns them, with finalOutput() of each:
---test data for factoryctl (the agent's poll report). One JSON object per line.
---Run it while CPUs craft something, best an item and a fluid drop at the same time.
---  cpu-dump <file> [--addr=<prefix>]
local component = require('component')
local shell = require('shell')

local args, opts = shell.parse(...)
local path = args[1]
if not path then
    io.stderr:write('usage: cpu-dump <file> [--addr=<prefix>]\n')
    return
end

local function findProxy()
    for _, ctype in ipairs({ 'me_interface', 'me_controller' }) do
        for address in component.list(ctype, true) do
            if type(opts.addr) ~= 'string' or address:sub(1, #opts.addr) == opts.addr then
                return component.proxy(address)
            end
        end
    end
    error('no me_interface or me_controller' .. (opts.addr and (' at ' .. opts.addr) or ''))
end

local function jsonString(s)
    return '"' .. s:gsub('[%c"\\]', function(c)
        local named = { ['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t' }
        return named[c] or ('\\u%04x'):format(c:byte())
    end) .. '"'
end

---JSON of plain data; `tag` (binary NBT) is left out, deep tables are cut at `depth` 3.
local function json(v, depth)
    local t = type(v)
    if t == 'string' then
        return jsonString(v)
    elseif t == 'number' then
        if v ~= v or v == math.huge or v == -math.huge then
            return jsonString(tostring(v))
        end
        return math.type(v) == 'integer' and tostring(v) or ('%.17g'):format(v)
    elseif t == 'boolean' then
        return tostring(v)
    elseif t == 'table' then
        if depth >= 3 then
            return jsonString('<table>')
        end
        local keys = {}
        for k in pairs(v) do
            if k ~= 'tag' then
                keys[#keys + 1] = k
            end
        end
        table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
        local parts = {}
        for _, k in ipairs(keys) do
            parts[#parts + 1] = jsonString(tostring(k)) .. ':' .. json(v[k], depth + 1)
        end
        return '{' .. table.concat(parts, ',') .. '}'
    end
    return jsonString('<' .. t .. '>') -- functions of the cpu object
end

local proxy = findProxy()
local out = assert(io.open(path, 'w'))
local busy = 0
for _, meta in ipairs(proxy.getCpus()) do
    local row = {}
    for k, v in pairs(meta) do
        if k ~= 'cpu' then
            row[k] = v
        end
    end
    -- Not `ok and final or ...`: a free CPU has no final output (nil), which is not an error.
    local ok, final = pcall(meta.cpu.finalOutput)
    if ok then
        row.finalOutput = final
    else
        row.finalOutput = 'error: ' .. tostring(final)
    end
    local okBusy, isBusy = pcall(meta.cpu.isBusy)
    if okBusy then
        row.isBusy = isBusy
    end
    if row.busy then
        busy = busy + 1
    end
    out:write(json(row, 0), '\n')
end
out:close()
print(('%d busy CPUs written to %s'):format(busy, path))
