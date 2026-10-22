---Loads both config parts and checks that they agree on recipe ids.
local bind = require('qft.bind')
local resource = require('qft.resource')

local config = {}

---@class qft.Config
---@field components table
---@field recipes table<qft.RecipeId, qft.Recipe>

---@param ids table<string, string>
---@param components table
---@param recipes table
---@return string[] # Problems found, empty when the parts agree.
function config.validate(ids, components, recipes)
    local known = {}
    for _, id in pairs(ids) do
        known[id] = true
    end

    local problems = {}
    local function check(tbl, where, other, otherWhere)
        for id in pairs(tbl) do
            if not known[id] then
                problems[#problems + 1] = ('%s: recipe "%s" is not in config/ids.lua'):format(where, id)
            elseif other[id] == nil then
                problems[#problems + 1] = ('%s: recipe "%s" is missing in %s'):format(where, id, otherWhere)
            end
        end
    end
    check(components.recipes or {}, 'components', recipes, 'recipes')
    check(recipes, 'recipes', components.recipes or {}, 'components')
    table.sort(problems)
    return problems
end

local function isInt(v, min, max)
    return math.type(v) == 'integer' and v >= min and (max == nil or v <= max)
end

local kMaxTier = 4

---Checks the values the focusing math depends on. Ids are checked by config.validate.
---@param components table
---@param recipes table<string, qft.Recipe>
---@return string[] # Problems found, empty when the values are usable.
function config.checkValues(components, recipes)
    local problems = {}
    local function add(fmt, ...)
        problems[#problems + 1] = fmt:format(...)
    end

    if not isInt(components.shieldingTier, 1, kMaxTier) then
        add('components: shieldingTier must be an integer 1..%d', kMaxTier)
    end
    for name, plasma in pairs(components.plasma or {}) do
        if not bind.isBinding(plasma.hatch) then
            add('components.plasma.%s: hatch must be a binding', name)
        end
        if type(plasma.fluid) ~= 'string' then
            add('components.plasma.%s: fluid must be a label', name)
        end
    end
    for id, parts in pairs(components.recipes or {}) do
        if not bind.isBinding(parts.bus) then
            add('components.recipes.%s: bus is required', id)
        end
        if parts.hatch ~= nil and not bind.isBinding(parts.hatch) then
            add('components.recipes.%s: hatch must be a binding or absent', id)
        end
    end
    for id, recipe in pairs(recipes) do
        if not isInt(recipe.tier, 1, kMaxTier) then
            add('recipes.%s: tier must be an integer 1..%d', id, kMaxTier)
        end
        if not isInt(recipe.catalysts, 1) then
            add('recipes.%s: catalysts must be an integer >= 1', id)
        end
        local outputs = recipe.outputs or {}
        if #outputs == 0 then
            add('recipes.%s: no outputs', id)
        end
        local seen = {}
        for i, out in ipairs(outputs) do
            if not isInt(out.circuit, 1) then
                add('recipes.%s.outputs[%d]: circuit must be an integer >= 1', id, i)
            elseif out.circuit > #outputs then
                -- The chances depend on the number of outputs: every output must be listed.
                add('recipes.%s.outputs[%d]: circuit %d > %d outputs listed; list every output',
                    id, i, out.circuit, #outputs)
            elseif seen[out.circuit] then
                add('recipes.%s.outputs[%d]: circuit %d is already used by outputs[%d]',
                    id, i, out.circuit, seen[out.circuit])
            else
                seen[out.circuit] = i
            end
            if out.target ~= nil and not (type(out.target) == 'number' and out.target > 0) then
                add('recipes.%s.outputs[%d]: target must be a number > 0 or absent', id, i)
            end
            if out.priority ~= nil and not (type(out.priority) == 'number' and out.priority > 0) then
                add('recipes.%s.outputs[%d]: priority must be a number > 0 or absent', id, i)
            end
        end
    end
    table.sort(problems)
    return problems
end

---Turns every output entry (`item = <label>` or `fluid = <label>`) into a qft.Resource,
---stored as `out.resource`; the rest of the program works with that only.
---@param recipes table<string, qft.Recipe>
---@return string[] problems
function config.normalize(recipes)
    local problems = {}
    for id, recipe in pairs(recipes) do
        for i, out in ipairs(recipe.outputs or {}) do
            local res, err = resource.fromSpec(out)
            if res then
                out.resource = res
            else
                problems[#problems + 1] = ('recipes.%s.outputs[%d]: %s'):format(id, i, err)
            end
        end
    end
    table.sort(problems)
    return problems
end

---@return qft.Config? cfg
---@return string[] problems
function config.load()
    local cfg = {
        components = require('config.components'),
        recipes = require('config.recipes'),
    }
    local problems = config.validate(require('config.ids'), cfg.components, cfg.recipes)
    if #problems == 0 then
        problems = config.checkValues(cfg.components, cfg.recipes)
    end
    if #problems == 0 then
        problems = config.normalize(cfg.recipes)
    end
    if #problems > 0 then
        return nil, problems
    end
    return cfg, problems
end

return config
