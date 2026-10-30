---JSON encoder for the agent's reports; the server's answers are Lua tables, so no decoder.
---A table is an array when it is empty or has [1], an object otherwise (string keys, sorted
---for stable output). Counts and ids are integers (an integral float is written as one);
---fractions are only times (computer.uptime()). nan and inf are refused.
local json = {}

local kNamed = { ['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t' }

local function encodeString(s)
    return '"' .. s:gsub('[%c"\\]', function(c)
        return kNamed[c] or ('\\u%04x'):format(c:byte())
    end) .. '"'
end

local encode

local function encodeTable(t)
    if next(t) == nil or t[1] ~= nil then
        local parts = {}
        for i = 1, #t do
            parts[i] = encode(t[i])
        end
        return '[' .. table.concat(parts, ',') .. ']'
    end
    local keys = {}
    for k in pairs(t) do
        if type(k) ~= 'string' then
            error('json: object key is not a string: ' .. tostring(k), 0)
        end
        keys[#keys + 1] = k
    end
    table.sort(keys)
    local parts = {}
    for i, k in ipairs(keys) do
        parts[i] = encodeString(k) .. ':' .. encode(t[k])
    end
    return '{' .. table.concat(parts, ',') .. '}'
end

encode = function(v)
    local t = type(v)
    if t == 'string' then
        return encodeString(v)
    elseif t == 'boolean' then
        return tostring(v)
    elseif t == 'number' then
        -- OC hands out sizes as floats; an integral one is written as an integer.
        if math.type(v) == 'integer' then
            return tostring(v)
        end
        if v ~= v or v == math.huge or v == -math.huge then
            error('json: not a finite number: ' .. tostring(v), 0)
        end
        if v == math.floor(v) and v >= -2 ^ 63 and v < 2 ^ 63 then
            return ('%d'):format(v)
        end
        return ('%.14g'):format(v)
    elseif t == 'table' then
        return encodeTable(v)
    end
    error('json: cannot encode a ' .. t, 0)
end

---@param value any # strings, booleans, integers and tables of them
---@return string
function json.encode(value)
    return encode(value)
end

return json
