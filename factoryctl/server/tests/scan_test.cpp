#include "domain/scan.hpp"

#include <userver/utest/utest.hpp>

namespace {

using namespace factoryctl::domain;
using Merged = std::vector<ScannedGoods>;

TEST(Scan, ItemKeyHasNoVariant) {
  EXPECT_EQ(keyOf({"gregtech:gt.metaitem.01", 2017, "Sodium Dust", 1, ""}),
            (ItemKey{"gregtech:gt.metaitem.01", 2017, ""}));
}

TEST(Scan, DropKeyHasTheFluid) {
  EXPECT_EQ(keyOf({"ae2fc:fluid_drop", 0, "drop of Molten Platinum", 1,
                   "molten.platinum"}),
            (ItemKey{"ae2fc:fluid_drop", 0, "molten.platinum"}));
}

TEST(Scan, FluidKeyIsTheDropOne) {
  // GTNH 2.9: AE stores fluids as fluids, not drops. The key stays the drop's,
  // so stock, targets and icons do not depend on the game version.
  EXPECT_EQ(keyOf({"plasma.niobium", 0, "Niobium Plasma", 1, "plasma.niobium"}),
            (ItemKey{"ae2fc:fluid_drop", 0, "plasma.niobium"}));
}

TEST(Scan, DropWithoutFluidFallsBackToLabel) {
  // Never seen in game; summing every drop under one key would be worse.
  EXPECT_EQ(keyOf({"ae2fc:fluid_drop", 0, "drop of Water", 1, ""}),
            (ItemKey{"ae2fc:fluid_drop", 0, "drop of Water"}));
}

TEST(Scan, MergeKeepsDistinctItems) {
  const ScannedStack stacks[] = {
      {"minecraft:stick", 0, "Stick", 5, ""},
      {"ae2fc:fluid_drop", 0, "drop of Hydrogen", 1000, "hydrogen"},
      {"ae2fc:fluid_drop", 0, "drop of Oxygen", 0, "oxygen"},
  };
  EXPECT_EQ(
      mergeBatch(stacks),
      (Merged{{{"minecraft:stick", 0, ""}, "Stick", 5},
              {{"ae2fc:fluid_drop", 0, "hydrogen"}, "drop of Hydrogen", 1000},
              {{"ae2fc:fluid_drop", 0, "oxygen"}, "drop of Oxygen", 0}}));
}

TEST(Scan, MergeSumsStacksOfOneKey) {
  // Items told apart by NBT only, or a stored stack next to its craftable
  // entry (IC2:blockElectric:1 in game: size 1, and size 0 craftable).
  const ScannedStack stacks[] = {
      {"minecraft:enchanted_book", 0, "Enchanted Book", 3, ""},
      {"minecraft:stick", 0, "Stick", 1, ""},
      {"minecraft:enchanted_book", 0, "Enchanted Book", 4, ""},
  };
  EXPECT_EQ(mergeBatch(stacks),
            (Merged{{{"minecraft:enchanted_book", 0, ""}, "Enchanted Book", 7},
                    {{"minecraft:stick", 0, ""}, "Stick", 1}}));
}

TEST(Scan, MergeIsCraftableIfAnyStackIs) {
  // In game: a stored MFE (NBT, not craftable) next to its craftable entry.
  const ScannedStack stacks[] = {
      {"IC2:blockElectric", 1, "MFE", 1, "", false},
      {"IC2:blockElectric", 1, "MFE", 0, "", true},
      {"minecraft:dirt", 0, "Dirt", 900, "", false},
  };
  EXPECT_EQ(mergeBatch(stacks),
            (Merged{{{"IC2:blockElectric", 1, ""}, "MFE", 1, true},
                    {{"minecraft:dirt", 0, ""}, "Dirt", 900, false}}));
}

TEST(Scan, MergeOfNothing) { EXPECT_TRUE(mergeBatch({}).empty()); }

}  // namespace
