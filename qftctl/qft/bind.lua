---Descriptors for config slots that must be bound to a concrete component address.
---The config declares *what* to find; the addresses themselves live in the bindings file.
local bind = {}

---@class qft.Binding
---@field type string|string[] # OC component type(s), e.g. 'gt_machine', or
---                            # { 'fluid_interface', 'me_controller' }: any of them will do.
---@field prompt string # What to tell the user while waiting for the component.

---@class qft.BindingSlot
---@field path string # Dotted path in the config, used as the key in the bindings file.
---@field binding qft.Binding

-- Used only as a type tag: a table is a binding iff it carries this metatable.
-- A plain marker field could collide with user data; a private metatable cannot.
local Binding = {}

---A component the user identifies by hand: setup mode asks them to (re)connect it.
---@param spec { type: string|string[], prompt: string }
---@return qft.Binding
function bind.manual(spec)
    -- Unknown fields would be dropped here and the slot is replaced by a proxy on resolve,
    -- so data placed in a binding is lost silently. Keep such data next to the binding.
    for k in pairs(spec) do
        assert(k == 'type' or k == 'prompt', 'bind.manual: unknown field ' .. tostring(k))
    end
    assert(type(spec.type) == 'string' or (type(spec.type) == 'table' and #spec.type > 0),
        'bind.manual: type is required: a component type or a list of them')
    assert(type(spec.prompt) == 'string', 'bind.manual: prompt is required')
    return setmetatable({ type = spec.type, prompt = spec.prompt }, Binding)
end

---The component types a binding takes.
---@param binding qft.Binding
---@return string[]
function bind.types(binding)
    return type(binding.type) == 'table' and binding.type or { binding.type }
end

---@param binding qft.Binding
---@param ctype string?
---@return boolean
function bind.accepts(binding, ctype)
    for _, t in ipairs(bind.types(binding)) do
        if t == ctype then
            return true
        end
    end
    return false
end

---@param value any
---@return boolean
function bind.isBinding(value)
    return getmetatable(value) == Binding
end

-- Numbers first, then strings; pairs() order is unspecified, prompts must not jump around.
local function keyLess(a, b)
    local ta, tb = type(a), type(b)
    if ta ~= tb then
        return ta == 'number'
    end
    return a < b
end

local function sortedKeys(tbl)
    local keys = {}
    for k in pairs(tbl) do
        keys[#keys + 1] = k
    end
    table.sort(keys, keyLess)
    return keys
end

---Walks the config and returns every binding with its path, in a stable order.
---@param config table
---@return qft.BindingSlot[]
function bind.collect(config)
    local slots, seen = {}, {}
    local function walk(tbl, prefix)
        for _, k in ipairs(sortedKeys(tbl)) do
            local v = tbl[k]
            local seg = tostring(k)
            local path = prefix and (prefix .. '.' .. seg) or seg
            if bind.isBinding(v) then
                assert(not seen[path], 'duplicate binding path: ' .. path)
                seen[path] = true
                slots[#slots + 1] = { path = path, binding = v }
            elseif type(v) == 'table' then
                walk(v, path)
            end
        end
    end
    walk(config, nil)
    return slots
end

---Returns a deep copy of config where every binding is replaced by fn(path, binding).
---Paths match the ones bind.collect produces.
---@param config table
---@param fn fun(path: string, binding: qft.Binding): any
---@return table
function bind.map(config, fn)
    local function copy(tbl, prefix)
        local out = {}
        for k, v in pairs(tbl) do
            local seg = tostring(k)
            local path = prefix and (prefix .. '.' .. seg) or seg
            if bind.isBinding(v) then
                out[k] = fn(path, v)
            elseif type(v) == 'table' then
                out[k] = copy(v, path)
            else
                out[k] = v
            end
        end
        return out
    end
    return copy(config, nil)
end

return bind
