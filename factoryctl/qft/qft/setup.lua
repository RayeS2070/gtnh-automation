---Setup mode: walks the config and binds every slot that is unbound or stale.
local component = require('component')
local event = require('event')
local bind = require('qft.bind')
local store = require('qft.store')

local setup = {}

local kEnter = 13
local kSkip = string.byte('s')

---@param binding qft.Binding
---@return table<string, boolean>
local function addressSet(binding)
    local set = {}
    for _, ctype in ipairs(bind.types(binding)) do
        for address in component.list(ctype, true) do
            set[address] = true
        end
    end
    return set
end

---Addresses already taken by other slots, so one component is not bound twice.
---@param saved qft.Saved
---@param exceptPath string
---@return table<string, string> # address -> path
local function usedAddresses(saved, exceptPath)
    local used = {}
    for path, entry in pairs(saved) do
        if path ~= exceptPath and component.type(entry.address) == entry.type then
            used[entry.address] = path
        end
    end
    return used
end

---Waits until the user connects the component (component_added),
---or presses Enter after connecting it (diff against a snapshot taken before).
---@param path string
---@param binding qft.Binding
---@param used table<string, string>
---@return string? address
---@return string? err # 'skipped' or 'interrupted' when nothing was bound.
local function waitForManual(path, binding, used)
    local before = addressSet(binding)
    local kinds = table.concat(bind.types(binding), ' or ')
    print(('[%s] %s'):format(path, binding.prompt))
    print(('  Connect the %s now, or connect it and press Enter. [s] skip.'):format(kinds))

    while true do
        local name, a1, a2 = event.pullMultiple('component_added', 'key_down', 'interrupted')
        local candidate

        if name == 'interrupted' then
            return nil, 'interrupted'
        elseif name == 'component_added' and bind.accepts(binding, a2) then
            candidate = a1
        elseif name == 'key_down' and a2 == kSkip then
            return nil, 'skipped'
        elseif name == 'key_down' and a2 == kEnter then
            local fresh = {}
            for address in pairs(addressSet(binding)) do
                if not before[address] and not used[address] then
                    fresh[#fresh + 1] = address
                end
            end
            if #fresh == 1 then
                candidate = fresh[1]
            elseif #fresh == 0 then
                print('  No new ' .. kinds .. ' appeared. Reconnect it and try again.')
            else
                print(('  %d new components appeared, cannot tell which one. Connect one at a time.')
                    :format(#fresh))
                before = addressSet(binding)
            end
        end

        if candidate then
            if used[candidate] then
                print(('  %s is already bound to %s, ignoring.'):format(candidate, used[candidate]))
            else
                return candidate
            end
        end
    end
end

---@class qft.SetupOptions
---@field all boolean? # Rebind every slot, not only the unbound or stale ones.
---@field path string? # Bindings file, store.defaultPath by default.

---@param config table
---@param opts qft.SetupOptions?
---@return boolean ok
---@return string[] unbound # Paths left without a valid binding.
function setup.run(config, opts)
    opts = opts or {}
    local path = opts.path or store.defaultPath
    ---@type qft.Saved
    local saved = {}
    -- --all starts from an empty set: old bindings must not block addresses for new ones.
    if not opts.all then
        local loaded, err = store.load(path)
        if loaded then
            saved = loaded
        else
            print(tostring(err) .. ', starting from scratch')
        end
    end

    -- Forget entries for slots no longer in the config: they would only block their addresses.
    local slots = bind.collect(config)
    local known = {}
    for _, slot in ipairs(slots) do
        known[slot.path] = true
    end
    for slotPath in pairs(saved) do
        if not known[slotPath] then
            saved[slotPath] = nil
        end
    end

    local unbound = {}
    for _, slot in ipairs(slots) do
        local b = slot.binding
        local entry = saved[slot.path]

        if store.isValid(entry, b) then
            print(('[%s] ok: %s'):format(slot.path, entry.address))
        else
            local used = usedAddresses(saved, slot.path)
            local address, why = waitForManual(slot.path, b, used)

            if address then
                saved[slot.path] = { address = address, type = component.type(address) }
                -- Save after every slot: a crash halfway must not lose what is already bound.
                local ok, saveErr = store.save(path, saved)
                if not ok then
                    print('cannot write ' .. path .. ': ' .. tostring(saveErr))
                    return false, { slot.path }
                end
                print(('[%s] bound: %s'):format(slot.path, address))
            else
                print(('[%s] not bound: %s'):format(slot.path, why))
                unbound[#unbound + 1] = slot.path
                if why == 'interrupted' then
                    return false, unbound
                end
            end
        end
    end
    return #unbound == 0, unbound
end

return setup
