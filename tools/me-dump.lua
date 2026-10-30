---Dumps the item stacks of an ME network as they come from getItemsInNetworkById, every field,
---one JSON object per line: test data for factoryctl (server scan tests, a fake me_interface
---for the agent tests).
---  me-dump <file> [--addr=<prefix>] [--max=<id>] [--chunk=<n>] [--slow=<from>-<to>]
---          [--slow-chunk=<n>] [--from=<id>] [--to=<id>]
---    file     where to write (JSON lines)
---    --addr   the interface/controller whose address starts with this (default: the first)
---    --max --chunk --slow --slow-chunk  id chunks as in `items --byid`, the exporter's scheme
---             (defaults 11500, 1000, 250-320, 5): chunks over ids 250..320 must stay small
---    --from --to  only this id range (default 0..max)
---Each line also has "_chunk": the first id of the call that returned the stack, so the dump
---can be replayed batch by batch. Only one chunk is held in memory.
local component = require('component')
local computer = require('computer')
local shell = require('shell')

local args, opts = shell.parse(...)
local path = args[1]
if not path then
    io.stderr:write('usage: me-dump <file> [--addr=<prefix>] [--from=<id>] [--to=<id>]\n')
    return
end
local maxId = tonumber(opts.max) or 11500
local chunk = tonumber(opts.chunk) or 1000
local slowFrom, slowTo = 250, 320
if type(opts.slow) == 'string' then
    slowFrom, slowTo = opts.slow:match('^(%d+)%-(%d+)$')
    slowFrom, slowTo = tonumber(slowFrom), tonumber(slowTo)
    assert(slowFrom and slowTo and slowFrom <= slowTo, '--slow must look like 250-320')
end
local slowChunk = tonumber(opts['slow-chunk']) or 5
local fromId = tonumber(opts.from) or 0
local toId = math.min(tonumber(opts.to) or maxId, maxId)

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

---The last id of the chunk that starts at `first` (same rule as items.lua).
local function chunkEnd(first)
    if first >= slowFrom and first <= slowTo then
        return math.min(first + slowChunk - 1, slowTo, toId)
    end
    local last = math.min(first + chunk - 1, toId)
    if first < slowFrom and last >= slowFrom then
        last = slowFrom - 1
    end
    return last
end

local function jsonString(s)
    return '"' .. s:gsub('[%c"\\]', function(c)
        local named = { ['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t' }
        return named[c] or ('\\u%04x'):format(c:byte())
    end) .. '"'
end

---JSON of a value; tables of a stack are flattened one level deep at most (tag info and the
---like), deeper ones are written as a note: we want to see the fields, not every byte.
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
        if depth >= 2 then
            return jsonString('<table>')
        end
        local keys = {}
        for k in pairs(v) do
            keys[#keys + 1] = k
        end
        table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
        local parts = {}
        for _, k in ipairs(keys) do
            parts[#parts + 1] = jsonString(tostring(k)) .. ':' .. json(v[k], depth + 1)
        end
        return '{' .. table.concat(parts, ',') .. '}'
    end
    return jsonString('<' .. t .. '>')
end

local proxy = findProxy()
local out = assert(io.open(path, 'w'))
local started, stacks, calls = computer.uptime(), 0, 0

local first = fromId
while first <= toId do
    local last = chunkEnd(first)
    local ids = {}
    for id = first, last do
        ids[#ids + 1] = id
    end
    local ok, res = pcall(proxy.getItemsInNetworkById, ids)
    calls = calls + 1
    if not ok then
        out:close()
        error(('getItemsInNetworkById(%d..%d) failed: %s'):format(first, last, tostring(res)))
    end
    for _, stack in ipairs(res) do
        stack._chunk = first
        out:write(json(stack, 0), '\n')
        stacks = stacks + 1
    end
    res = nil
    os.sleep(0)
    first = last + 1
end
out:close()
print(('%d stacks from ids %d..%d in %d calls, %.0fs, free memory %d: %s'):format(stacks,
    fromId, toId, calls, computer.uptime() - started, computer.freeMemory(), path))
