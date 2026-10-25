---Recipe identifiers: the key that ties config/components.lua to config/recipes.lua.
---A new recipe needs an entry here, in components and in recipes; qft/config.lua checks that.

---@enum qft.RecipeId
local RecipeId = {
    RubberPolymer = 'rubber_polymer',
    PlatinumGroup = 'platinum_group',
    AdhesionPromoter = 'adhesion_promoter',
    InertNaquadah = 'simple_naquadah',
    InertEnrichedNaquadah = 'inert_enriched_naquadah'
}

-- A missing key in Lua is a silent nil; make a typo like RecipeId.RubberPolymr fail loudly.
return setmetatable(RecipeId, {
    __index = function(_, key)
        error('unknown recipe id: ' .. tostring(key), 2)
    end,
})
