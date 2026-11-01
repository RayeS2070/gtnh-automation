---The pumping settings: checked the same way whether they come from the server, the saved file
---or config.lua. A bad target is left out with a warning; bad numbers refuse the whole table.
local recipes = require('fp.recipes')

local settings = {}

local kBatchMax = 128 -- TileEntityModulePump.PUMP_BATCH_MAX

---@param raw table?
---@param warn fun(fmt: string, ...)
---@return fp.Settings? settings
---@return string? err
function settings.check(raw, warn)
    if type(raw) ~= 'table' then
        return nil, 'not a table'
    end
    local batch, threshold = raw.batch, raw.threshold
    if math.type(batch) ~= 'integer' and not (type(batch) == 'number' and batch % 1 == 0) then
        return nil, 'batch: not an integer'
    end
    if batch < 1 or batch > kBatchMax then
        return nil, ('batch: %d is not in 1..%d'):format(batch, kBatchMax)
    end
    if type(threshold) ~= 'number' or threshold <= 0 or threshold > 1 then
        return nil, 'threshold: not in (0, 1]'
    end
    local out = { version = math.tointeger(raw.version) or 0, batch = math.tointeger(batch),
        threshold = threshold, targets = {} }
    local seen = {}
    for i, t in ipairs(type(raw.targets) == 'table' and raw.targets or {}) do
        local fluid = type(t) == 'table' and t.fluid
        if not (type(fluid) == 'string' and recipes.byFluid[fluid]) then
            warn('settings: targets[%d]: no pump recipe for %s; left out', i, tostring(fluid))
        elseif seen[fluid] then
            warn('settings: %s twice; the first one is kept', fluid)
        elseif type(t.target) ~= 'number' or t.target < 0 then
            warn('settings: %s: bad target; left out', fluid)
        elseif type(t.priority or 1) ~= 'number' or (t.priority or 1) <= 0 then
            warn('settings: %s: bad priority; left out', fluid)
        else
            seen[fluid] = true
            out.targets[#out.targets + 1] = { fluid = fluid, target = t.target,
                priority = t.priority or 1, enabled = t.enabled ~= false }
        end
    end
    return out
end

---The table in the protocol's shape (and the saved file's).
---@param s fp.Settings
function settings.plain(s)
    local targets = {}
    for i, t in ipairs(s.targets) do
        targets[i] = { fluid = t.fluid, target = t.target, priority = t.priority,
            enabled = t.enabled }
    end
    return { version = s.version, batch = s.batch, threshold = s.threshold, targets = targets }
end

---@class fp.Store
---@field read fun(): string? # the saved text; nil: none
---@field write fun(text: string): boolean?, string?
---@field encode fun(value: table): string # serialization.serialize in game
---@field decode fun(text: string): any

---The saved settings (the last ones the server gave), else the defaults of config.lua.
---@param store fp.Store
---@param defaults table
---@param warn fun(fmt: string, ...)
---@return fp.Settings? settings
---@return string? from # 'saved' | 'defaults'
---@return string? err
function settings.load(store, defaults, warn)
    local text = store.read()
    if text then
        local ok, raw = pcall(store.decode, text)
        local s, err = settings.check(ok and raw or nil, warn)
        if s then
            return s, 'saved'
        end
        warn('saved settings refused (%s): the defaults of config.lua instead', tostring(err))
    end
    local d = {}
    for k, v in pairs(defaults) do
        d[k] = v
    end
    d.version = 0 -- never the server's
    local s, err = settings.check(d, warn)
    if not s then
        return nil, nil, 'config.lua defaults: ' .. tostring(err)
    end
    return s, 'defaults'
end

---@param store fp.Store
---@param s fp.Settings
function settings.save(store, s)
    return store.write(store.encode(settings.plain(s)))
end

return settings
