---What the Space Elevator pump modules can pump: (planet, gas) -> fluid. From GT5-Unofficial
---5.09.54.264 (GTNH 2.9.0), gtnhintergalactic/recipe/SpacePumpingRecipes.java. `rate` is the
---fluid of one recipe, in mB per second of a cycle for one parallel (a cycle of `batch` seconds
---gives rate * parallel * batch). `fluid` is the fluid's internal name, as AE and the factoryctl
---server key it: checked against the game's network for all but the ones marked.
local recipes = {}

---@class fp.Recipe
---@field planet integer
---@field gas integer
---@field fluid string
---@field label string
---@field rate integer

---@type fp.Recipe[]
recipes.list = {
    { planet = 2, gas = 1, fluid = 'chlorobenzene', label = 'Chlorobenzene', rate = 896000 },
    { planet = 3, gas = 1, fluid = 'endergoo', label = 'Ender Goo', rate = 32000 },
    -- Not seen in game: the name is GT's (LoaderGTBlockFluid).
    { planet = 3, gas = 2, fluid = 'liquid_extra_heavy_oil', label = 'Very Heavy Oil', rate = 1400000 },
    { planet = 3, gas = 3, fluid = 'lava', label = 'Lava', rate = 1800000 },
    -- Not seen in game: the name is GT's (UndergroundFluidNames).
    { planet = 3, gas = 4, fluid = 'gas_natural_gas', label = 'Natural Gas', rate = 1400000 },
    { planet = 4, gas = 1, fluid = 'sulfuricacid', label = 'Sulfuric Acid', rate = 784000 },
    { planet = 4, gas = 2, fluid = 'molten.iron', label = 'Molten Iron', rate = 896000 },
    { planet = 4, gas = 3, fluid = 'oil', label = 'Oil', rate = 1400000 },
    { planet = 4, gas = 4, fluid = 'liquid_heavy_oil', label = 'Heavy Oil', rate = 1792000 },
    { planet = 4, gas = 5, fluid = 'molten.lead', label = 'Molten Lead', rate = 896000 },
    -- Not seen in game: the name is GT's (LoaderGTBlockFluid).
    { planet = 4, gas = 6, fluid = 'liquid_medium_oil', label = 'Raw Oil', rate = 1400000 },
    -- Not seen in game: the name is GT's (LoaderGTBlockFluid).
    { planet = 4, gas = 7, fluid = 'liquid_light_oil', label = 'Light Oil', rate = 780000 },
    { planet = 4, gas = 8, fluid = 'carbondioxide', label = 'Carbon Dioxide', rate = 1680000 },
    { planet = 5, gas = 1, fluid = 'carbonmonoxide', label = 'Carbon Monoxide', rate = 4480000 },
    { planet = 5, gas = 2, fluid = 'helium-3', label = 'Helium-3', rate = 2800000 },
    { planet = 5, gas = 3, fluid = 'saltwater', label = 'Salt Water', rate = 2800000 },
    { planet = 5, gas = 4, fluid = 'helium', label = 'Helium', rate = 1400000 },
    -- Not seen in game: GT builds it as "LiquidOxygen", Forge keeps fluid names in lower case.
    { planet = 5, gas = 5, fluid = 'liquidoxygen', label = 'Liquid Oxygen', rate = 896000 },
    { planet = 5, gas = 6, fluid = 'neon', label = 'Neon', rate = 32000 },
    { planet = 5, gas = 7, fluid = 'argon', label = 'Argon', rate = 32000 },
    { planet = 5, gas = 8, fluid = 'krypton', label = 'Krypton', rate = 8000 },
    { planet = 5, gas = 9, fluid = 'methane', label = 'Methane', rate = 1792000 },
    { planet = 5, gas = 10, fluid = 'liquid_hydricsulfur', label = 'Hydrogen Sulfide', rate = 392000 },
    { planet = 5, gas = 11, fluid = 'ethane', label = 'Ethane', rate = 1194000 },
    { planet = 6, gas = 1, fluid = 'deuterium', label = 'Deuterium', rate = 1568000 },
    { planet = 6, gas = 2, fluid = 'tritium', label = 'Tritium', rate = 240000 },
    { planet = 6, gas = 3, fluid = 'ammonia', label = 'Ammonia', rate = 240000 },
    { planet = 6, gas = 4, fluid = 'xenon', label = 'Xenon', rate = 16000 },
    { planet = 6, gas = 5, fluid = 'ethylene', label = 'Ethylene', rate = 1792000 },
    { planet = 7, gas = 1, fluid = 'hydrofluoricacid_gt5u', label = 'Hydrofluoric Acid', rate = 672000 },
    { planet = 7, gas = 2, fluid = 'fluorine', label = 'Fluorine', rate = 1792000 },
    { planet = 7, gas = 3, fluid = 'nitrogen', label = 'Nitrogen', rate = 1792000 },
    { planet = 7, gas = 4, fluid = 'oxygen', label = 'Oxygen', rate = 1792000 },
    { planet = 8, gas = 1, fluid = 'hydrogen', label = 'Hydrogen', rate = 1568000 },
    { planet = 8, gas = 2, fluid = 'liquidair', label = 'Liquid Air', rate = 875000 },
    { planet = 8, gas = 3, fluid = 'molten.copper', label = 'Molten Copper', rate = 672000 },
    { planet = 8, gas = 4, fluid = 'unknowwater', label = 'Unknown Liquid', rate = 672000 },
    { planet = 8, gas = 5, fluid = 'ic2distilledwater', label = 'Distilled Water', rate = 17920000 },
    { planet = 8, gas = 6, fluid = 'radon', label = 'Radon', rate = 64000 },
    { planet = 8, gas = 7, fluid = 'molten.tin', label = 'Molten Tin', rate = 672000 },
}

---@type table<string, fp.Recipe>
recipes.byFluid = {}
for _, r in ipairs(recipes.list) do
    recipes.byFluid[r.fluid] = r
end

return recipes
