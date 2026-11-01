---Loads both config parts and checks that they agree on recipe ids.
local bind = require('qft.bind')
local resource = require('qft.resource')
local slots = require('qft.slots')

local config = {}

---@class qft.Config
---@field components table
---@field recipes table<qft.RecipeId, qft.Recipe>

---@param ids table<string, string>
---@param components table
---@param recipes table
---@return string[] # Problems found, empty when every id has a recipe and every recipe an id.
function config.validate(ids, components, recipes)
    local _ = components -- recipes no longer have components of their own (GTNH 2.9)
    local known = {}
    local problems = {}
    for _, id in pairs(ids) do
        known[id] = true
        if recipes[id] == nil then
            problems[#problems + 1] = ('ids: recipe "%s" is missing in recipes'):format(id)
        end
    end
    for id in pairs(recipes) do
        if not known[id] then
            problems[#problems + 1] = ('recipes: recipe "%s" is not in config/ids.lua'):format(id)
        end
    end
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
    local plasma = components.plasma
    if type(plasma) ~= 'table' or not bind.isBinding(plasma.hatch) then
        add('components.plasma: hatch must be a binding (one hatch, the program sets the plasma)')
    else
        for _, name in ipairs({ 'neptunium', 'fermium' }) do
            if type(plasma[name]) ~= 'string' then
                add('components.plasma.%s must be the label of the plasma', name)
            end
        end
    end
    for _, part in ipairs({ 'bus', 'hatch' }) do
        if not bind.isBinding(components[part]) then
            add('components: %s must be a binding (one stocking input %s for every recipe)', part,
                part)
        end
    end
    if components.recipes ~= nil then
        add('components: recipes are gone: one bus and one hatch serve every recipe now; their '
            .. 'inputs go to config/recipes.lua')
    end
    for id, recipe in pairs(recipes) do
        if not isInt(recipe.tier, 1, kMaxTier) then
            add('recipes.%s: tier must be an integer 1..%d', id, kMaxTier)
        end
        if not isInt(recipe.catalysts, 0) then
            add('recipes.%s: catalysts must be an integer >= 0', id)
        end
        local outputs = recipe.outputs or {}
        if #outputs == 0 then
            add('recipes.%s: no outputs', id)
        end
        local trigger = recipe.trigger
        if trigger ~= 'deficit' and trigger ~= 'surplus' then
            add("recipes.%s: trigger must be 'deficit' or 'surplus'", id)
        end
        -- Inputs set the slots of the bus and the hatch: every recipe has them.
        local inputs = recipe.inputs
        if type(inputs) ~= 'table' or #inputs == 0 then
            add('recipes.%s: inputs must be a list of { item|fluid = <label>[, amount = n] }: the '
                .. 'bus and the hatch are set from them', id)
        else
            local items, fluids = 0, 0
            for i, input in ipairs(inputs) do
                if input.amount ~= nil and not (type(input.amount) == 'number' and input.amount > 0) then
                    add('recipes.%s.inputs[%d]: amount must be a number > 0 or absent', id, i)
                elseif input.amount == nil and trigger == 'surplus' then
                    add('recipes.%s.inputs[%d]: a surplus recipe needs the amount of every input: '
                        .. 'it decides when the recipe runs', id, i)
                end
                if input.item ~= nil then
                    items = items + 1
                elseif input.fluid ~= nil then
                    fluids = fluids + 1
                end
            end
            if items > slots.kSize then
                add('recipes.%s: %d item inputs, the bus has %d slots', id, items, slots.kSize)
            end
            if fluids > slots.kSize then
                add('recipes.%s: %d fluid inputs, the hatch has %d slots', id, fluids, slots.kSize)
            end
        end
        local targets = 0
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
            if out.weight ~= nil and not (type(out.weight) == 'number' and out.weight >= 0) then
                add('recipes.%s.outputs[%d]: weight must be a number >= 0 or absent', id, i)
            end
            if out.target ~= nil then
                targets = targets + 1
            end
            -- Each field belongs to one trigger; a stray one means the trigger is wrong.
            if trigger == 'surplus' and (out.target ~= nil or out.priority ~= nil) then
                add('recipes.%s.outputs[%d]: target/priority are for deficit recipes, use weight',
                    id, i)
            elseif trigger == 'deficit' and out.weight ~= nil then
                add('recipes.%s.outputs[%d]: weight is for surplus recipes, use target/priority',
                    id, i)
            end
        end
        if trigger == 'deficit' and targets == 0 then
            add('recipes.%s: a deficit recipe needs a target on some output', id)
        end
    end
    table.sort(problems)
    return problems
end

---Turns every output and input entry (`item = <label>` or `fluid = <label>`) into a
---qft.Resource, stored as `entry.resource`; the rest of the program works with that only.
---@param recipes table<string, qft.Recipe>
---@return string[] problems
function config.normalize(recipes)
    local problems = {}
    for id, recipe in pairs(recipes) do
        for _, field in ipairs({ 'outputs', 'inputs' }) do
            for i, entry in ipairs(recipe[field] or {}) do
                local res, err = resource.fromSpec(entry)
                if res then
                    entry.resource = res
                else
                    problems[#problems + 1] = ('recipes.%s.%s[%d]: %s'):format(id, field, i, err)
                end
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
