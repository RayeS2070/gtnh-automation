---The catalysts in the bulk catalyst housing of this QFT: label = how many (the parallels).
---Every recipe of a listed catalyst (qft/catalog.lua) runs on this QFT; uncomment the ones it
---has. Edited by hand. Everything else is set on the factoryctl server (the QFT tab): which
---recipes may run, their trigger, the targets and weights of the outputs, the mode.
return {
    ['Adhesion Promoter Catalyst'] = 4,          -- Hyper-Stable Self-Healing Adhesive
    -- ['Advanced Naquadah Catalyst'] = 1,       -- Inert Naquadria Dust
    -- ['Algagenic Growth Promoter Catalyst'] = 1, -- T Ceti E Seaweed
    -- ['Biological Intelligence Catalyst'] = 1, -- Biocells
    -- ['Crystal Colorization Catalyst'] = 1,    -- Prismatic Naquadah Dust
    -- ['Hellish Force Catalyst'] = 1,           -- Netherite Scrap
    -- ['Plastic Polymer Catalyst'] = 1,         -- Molten Polyethylene (PE)
    ['Platinum Group Catalyst'] = 1,             -- Platinum, Palladium, Osmium, Rhodium Dust;
                                                 -- Iridium Dust from Iridium Metal Residue and from
                                                 -- Leach Residue; Molten Osmium (combs), Molten
                                                 -- Platinum (leaves)
    -- ['Radioactivity Catalyst'] = 1,           -- Thorium 232 Dust
    -- ['Rare-Earth Group Catalyst'] = 1,        -- Cerium Dust; Holmium Dust from Bastnasite and
                                                 -- from Cerium-Rich Mixture
    -- ['Raw Intelligence Catalyst'] = 1,        -- Stemcells
    ['Rubber Polymer Catalyst'] = 1,             -- Molten Silicone Rubber
    ['Simple Naquadah Catalyst'] = 16,           -- Inert Naquadah Dust, Inert Enriched Naquadah Dust
    -- ['Temporal Harmonizer Catalyst'] = 1,     -- Shirabon Dust
    -- ['Tita-Tungsten Indium Catalyst'] = 1,    -- Titanium Dust
    -- ['Ultimate Plasticizer Catalyst'] = 1,    -- Polyurethane Resin, Xenoxene
}
