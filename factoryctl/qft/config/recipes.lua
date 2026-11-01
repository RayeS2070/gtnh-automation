---Which recipes this QFT runs, and how. What a recipe takes and gives (inputs, outputs, tier) is in
---qft/catalog.lua, generated from the modpack's data: pick recipes by its ids. Edited by hand.
---
---  trigger     'deficit' keeps products stocked: runs when a tracked output is below its target;
---              'surplus' processes raw materials: runs while its inputs are in stock
---  catalysts   catalysts in the bulk catalyst housing = parallels; 0 = never run it
---  name        shown instead of the catalog's (its first output's label)
---  countInputs false: inputs are supplied without limit, not counted (deficit only)
---  outputs     by circuit (the output's place in the catalog = in NEI): deficit `target` (keep this
---              much; none = not tracked) and `priority` (urgency exponent, default 1); surplus
---              `weight` (how much Neptunium should favour it; none = 0). With the factoryctl server
---              these are only the first values: its settings (the QFT tab) replace them.
---Items count in pieces, fluids in mB. In Fluid Mode (components.fluidMode) a dust output with a
---molten form counts as that fluid.
return {
    ['hyper_stable_self_healing_adhesive'] = {
        name = 'Adhesion Promoter',
        trigger = 'deficit',
        catalysts = 4,
        outputs = {
            [1] = { target = 4096, priority = 3 },  -- Hyper-Stable Self-Healing Adhesive
            [2] = { priority = 1 },                 -- Ethyl Cyanoacrylate (Super Glue)
            [3] = { priority = 1 },                 -- Advanced Glue
            [4] = { target = 1e8, priority = 2 },   -- Molten Indalloy 140
            [5] = { target = 1e7, priority = 1 },   -- Molten Soldering Alloy
        },
    },
    ['molten_silicone_rubber'] = {
        name = 'Rubber Polymer',
        trigger = 'deficit',
        catalysts = 1,
        countInputs = false,
        outputs = {
            [1] = { target = 10e8, priority = 2 },  -- Molten Silicone Rubber
            [2] = { target = 10e6, priority = 1 },  -- Molten Styrene-Butadiene Rubber
            [3] = { target = 10e8, priority = 2 },  -- Molten Polyphenylene Sulfide
            [4] = { target = 10e6, priority = 1 },  -- Molten Rubber
        },
    },
    ['platinum_dust'] = {
        name = 'Platinum Group',
        trigger = 'surplus',
        catalysts = 1,
        outputs = {
            [1] = { weight = 100 },  -- Platinum
            [2] = { weight = 100 },  -- Palladium
            [3] = { weight = 200 },  -- Iridium
            [4] = { weight = 200 },  -- Osmium
            [5] = { weight = 50 },   -- Rhodium
            [6] = { weight = 50 },   -- Ruthenium
        },
    },
    ['inert_naquadah_dust'] = {
        name = 'Simple Naquadah',
        trigger = 'surplus',
        catalysts = 16,
        outputs = {
            [1] = { weight = 600 },  -- Inert Naquadah Dust
            [2] = { weight = 100 },  -- Titanium
            [3] = { weight = 100 },  -- Adamantium
            [4] = { weight = 200 },  -- Gallium
        },
    },
    ['inert_enriched_naquadah_dust'] = {
        name = 'Inert Enriched Naquadah',
        trigger = 'surplus',
        catalysts = 4,
        outputs = {
            [1] = { weight = 900 },  -- Inert Enriched Naquadah Dust
            [2] = { weight = 100 },  -- Trinium
            [3] = { weight = 0 },    -- Waste Liquid
        },
    },
}
