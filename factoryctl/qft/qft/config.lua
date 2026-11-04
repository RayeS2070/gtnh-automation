---Loads the config: the components, and the recipes built from the catalog and the player's
---choice; checks the values.
local bind = require('qft.bind')
local resource = require('qft.resource')
local slots = require('qft.slots')

local config = {}

---@class qft.Config
---@field components table
---@field recipes table<qft.RecipeId, qft.Recipe>

---Builds the recipes the program runs from the catalog (qft/catalog.lua, generated) and the
---catalysts the QFT has (config/catalysts.lua: catalyst label -> count): every catalog recipe
---of a listed catalyst, with that many parallels. How a recipe runs (trigger, countInputs,
---targets, weights) is a setting of the server (qft.link): until it is applied, a recipe is a
---deficit one that tracks nothing, so it never runs. In Fluid Mode (components.fluidMode) a
---dust output with a molten form is counted as that fluid.
---@param catalog table<string, table>
---@param catalysts table<string, integer> # config/catalysts.lua
---@param fluidMode boolean
---@return table<string, qft.Recipe> recipes
---@return string[] problems
function config.build(catalog, catalysts, fluidMode)
    local recipes, problems = {}, {}
    local known = {}
    for _, base in pairs(catalog) do known[base.catalyst] = true end
    for label in pairs(catalysts) do
        if catalog[label] then
            problems[#problems + 1] = ('catalysts: %s is a recipe id, the old config/recipes.lua; '
                .. 'list the catalysts instead (their labels)'):format(label)
        elseif not known[label] then
            problems[#problems + 1] = ('catalysts: no recipe takes %s (the labels are the '
                .. '`catalyst` of qft/catalog.lua)'):format(tostring(label))
        end
    end
    for id, base in pairs(catalog) do
        local count = catalysts[base.catalyst]
        if count ~= nil then
            local inputs = {}
            for i, input in ipairs(base.inputs) do
                inputs[i] = { [input.item and 'item' or 'fluid'] = input.item or input.fluid,
                    name = input.name, damage = input.damage, amount = input.amount }
            end
            local outputs = {}
            for i, out in ipairs(base.outputs) do
                local entry
                if fluidMode and out.molten then
                    entry = { fluid = out.molten.fluid, name = out.molten.name }
                else
                    entry = { [out.item and 'item' or 'fluid'] = out.item or out.fluid,
                        name = out.name, damage = out.damage }
                end
                entry.circuit = i
                outputs[i] = entry
            end
            recipes[id] = { name = base.name, trigger = 'deficit', tier = base.tier,
                catalyst = base.catalyst, catalysts = count, inputs = inputs, outputs = outputs }
        end
    end
    table.sort(problems)
    return recipes, problems
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

    if type(components.server) ~= 'string' then
        add('components: server must be the URL of the factoryctl server: the settings come from it')
    end
    if components.name ~= nil and (type(components.name) ~= 'string' or components.name == '') then
        add('components: name must be a non-empty string or nil (qft@x,y,z)')
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
        add('components: recipes are gone: one bus and one hatch serve every recipe now')
    end
    if components.fluidMode ~= nil and type(components.fluidMode) ~= 'boolean' then
        add('components: fluidMode must be true or false')
    end
    for id, recipe in pairs(recipes) do
        if not isInt(recipe.tier, 1, kMaxTier) then
            add('recipes.%s: tier must be an integer 1..%d', id, kMaxTier)
        end
        if not isInt(recipe.catalysts, 0) then
            add("catalysts['%s'] = %s: must be an integer >= 0", tostring(recipe.catalyst),
                tostring(recipe.catalysts))
        end
        if #recipe.outputs == 0 then
            add('recipes.%s: no outputs', id)
        end
        -- Inputs set the slots of the bus and the hatch, and say when a recipe can run.
        local items, fluids = 0, 0
        for i, input in ipairs(recipe.inputs) do
            if not (type(input.amount) == 'number' and input.amount > 0) then
                add('recipes.%s.inputs[%d]: amount must be a number > 0', id, i)
            end
            if input.item ~= nil then
                items = items + 1
            else
                fluids = fluids + 1
            end
        end
        if #recipe.inputs == 0 then
            add('recipes.%s: no inputs: the bus and the hatch are set from them', id)
        end
        if items > slots.kSize then
            add('recipes.%s: %d item inputs, the bus has %d slots', id, items, slots.kSize)
        end
        if fluids > slots.kSize then
            add('recipes.%s: %d fluid inputs, the hatch has %d slots', id, fluids, slots.kSize)
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
    local components = require('config.components')
    local recipes, problems = config.build(require('qft.catalog'), require('config.catalysts'),
        components.fluidMode == true)
    local cfg = { components = components, recipes = recipes }
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
