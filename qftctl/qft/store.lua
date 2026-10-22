---The bindings file: binding path -> component address, written by setup mode.
local serialization = require('serialization')
local component = require('component')
local bind = require('qft.bind')

local store = {}

---@alias qft.Saved table<string, { address: string, type: string }>

store.defaultPath = '/etc/qftctl.bindings'

---A missing file is not an error, it just means nothing is bound yet.
---@param path string
---@return qft.Saved? saved
---@return string? err
function store.load(path)
    local f = io.open(path, 'r')
    if not f then
        return {}
    end
    local text = f:read('*a')
    f:close()
    local data = serialization.unserialize(text)
    if type(data) ~= 'table' then
        return nil, 'bindings file is corrupt: ' .. path
    end
    return data
end

---@param path string
---@param saved qft.Saved
---@return boolean? ok
---@return string? err
function store.save(path, saved)
    local f, err = io.open(path, 'w')
    if not f then
        return nil, err
    end
    f:write(serialization.serialize(saved))
    f:close()
    return true
end

---Is the saved entry still a live component of the expected type?
---@param entry { address: string, type: string }?
---@param binding qft.Binding
---@return boolean
function store.isValid(entry, binding)
    return entry ~= nil
        and entry.type == binding.type
        and component.type(entry.address) == binding.type
end

---Builds the runtime config: a copy of config with every binding replaced by a proxy.
---@param config table
---@param saved qft.Saved
---@return table? resolved # nil when anything is unbound or stale.
---@return string[] missing # Paths that need setup.
function store.resolve(config, saved)
    local missing = {}
    local resolved = bind.map(config, function(path, binding)
        local entry = saved[path]
        if not store.isValid(entry, binding) then
            missing[#missing + 1] = path
            return nil
        end
        return component.proxy(entry.address)
    end)
    if #missing > 0 then
        table.sort(missing)
        return nil, missing
    end
    return resolved, missing
end

return store
