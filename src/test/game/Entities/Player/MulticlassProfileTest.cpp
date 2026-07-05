/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "MulticlassProfile.h"
#include "gtest/gtest.h"

TEST(MulticlassProfile, IsValidClassId_acceptsPlayableRejectsGapAndZero)
{
    EXPECT_TRUE(MulticlassProfile::IsValidClassId(1));    // Warrior
    EXPECT_TRUE(MulticlassProfile::IsValidClassId(11));   // Druid
    EXPECT_FALSE(MulticlassProfile::IsValidClassId(0));   // CLASS_NONE
    EXPECT_FALSE(MulticlassProfile::IsValidClassId(10));  // unused gap
    EXPECT_FALSE(MulticlassProfile::IsValidClassId(12));  // out of range
}

TEST(MulticlassProfile, AddOwnedClass_addsValidRejectsInvalidAndDuplicate)
{
    MulticlassProfile p;
    EXPECT_TRUE(p.AddOwnedClass(1, 40, 500));
    EXPECT_TRUE(p.HasOwnedClass(1));
    EXPECT_EQ(p.GetClassLevel(1), 40u);
    EXPECT_FALSE(p.AddOwnedClass(1));    // duplicate
    EXPECT_FALSE(p.AddOwnedClass(10));   // invalid id
    EXPECT_FALSE(p.HasOwnedClass(8));    // never added
    EXPECT_EQ(p.GetClassLevel(8), 0u);   // not owned -> 0
}

TEST(MulticlassProfile, GetOwnedClasses_returnsAllOwnedInInsertionOrder)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);
    p.AddOwnedClass(8);
    p.AddOwnedClass(11);
    EXPECT_EQ(p.GetOwnedClasses(), (std::vector<uint8>{ 1, 8, 11 }));
}

TEST(MulticlassProfile, SetClassProgress_updatesOwnedFailsForUnowned)
{
    MulticlassProfile p;
    p.AddOwnedClass(8, 1, 0);
    EXPECT_TRUE(p.SetClassProgress(8, 20, 1234));
    EXPECT_EQ(p.GetClassLevel(8), 20u);
    EXPECT_FALSE(p.SetClassProgress(4, 5, 0));  // rogue not owned
}

TEST(MulticlassProfile, ActiveCeiling_defaultEffectiveThreeAndClampsToOne)
{
    MulticlassProfile p;
    EXPECT_EQ(p.GetMaxActiveClasses(), 3u);  // default: min(unlocked 11, ceiling 3)
    p.SetActiveCeiling(0);
    EXPECT_EQ(p.GetMaxActiveClasses(), 1u);  // ceiling floored to 1
    p.SetActiveCeiling(9);
    EXPECT_EQ(p.GetMaxActiveClasses(), 9u);  // min(unlocked 11, ceiling 9)
}

TEST(MulticlassProfile, EffectiveCap_isMinOfUnlockedAndCeiling)
{
    MulticlassProfile p;
    p.SetActiveCeiling(6);
    p.SetUnlockedSlots(2);
    EXPECT_EQ(p.GetMaxActiveClasses(), 2u);  // unlocked binds
    p.SetUnlockedSlots(9);
    EXPECT_EQ(p.GetMaxActiveClasses(), 6u);  // ceiling binds
    p.SetActiveCeiling(1);
    EXPECT_EQ(p.GetMaxActiveClasses(), 1u);
}

TEST(MulticlassProfile, SetUnlockedSlots_clampsToOneAndMax)
{
    MulticlassProfile p;
    p.SetActiveCeiling(11);
    p.SetUnlockedSlots(0);
    EXPECT_EQ(p.GetUnlockedSlots(), 1u);      // floored
    EXPECT_EQ(p.GetMaxActiveClasses(), 1u);
    p.SetUnlockedSlots(50);
    EXPECT_EQ(p.GetUnlockedSlots(), 11u);     // capped at kSlotCapacityMax
    EXPECT_EQ(p.GetMaxActiveClasses(), 11u);
}

TEST(MulticlassProfile, RaiseUnlockedTo_isMonotonicAndReportsChange)
{
    MulticlassProfile p;
    p.SetActiveCeiling(11);
    p.SetUnlockedSlots(2);
    EXPECT_FALSE(p.RaiseUnlockedTo(1));    // lower target -> no change
    EXPECT_EQ(p.GetUnlockedSlots(), 2u);
    EXPECT_FALSE(p.RaiseUnlockedTo(2));    // equal target -> no change
    EXPECT_TRUE(p.RaiseUnlockedTo(4));     // higher -> raises and reports change
    EXPECT_EQ(p.GetUnlockedSlots(), 4u);
    EXPECT_TRUE(p.RaiseUnlockedTo(50));    // clamps to kSlotCapacityMax, still a change
    EXPECT_EQ(p.GetUnlockedSlots(), 11u);
}

TEST(MulticlassProfile, SlotCapacityForLevel_mapsLevelBands)
{
    EXPECT_EQ(MulticlassProfile::SlotCapacityForLevel(1), 1u);
    EXPECT_EQ(MulticlassProfile::SlotCapacityForLevel(4), 1u);
    EXPECT_EQ(MulticlassProfile::SlotCapacityForLevel(5), 2u);
    EXPECT_EQ(MulticlassProfile::SlotCapacityForLevel(9), 2u);
    EXPECT_EQ(MulticlassProfile::SlotCapacityForLevel(10), 11u);
    EXPECT_EQ(MulticlassProfile::SlotCapacityForLevel(80), 11u);
}

TEST(MulticlassProfile, GetMaxOwnedLevel_returnsHighestPoolLevelActiveOrBenched)
{
    MulticlassProfile p;
    EXPECT_EQ(p.GetMaxOwnedLevel(), 0u);      // empty pool
    p.Load({ { 1, 20, 0 }, { 8, 45, 0 }, { 4, 5, 0 } }, { 1 });  // mage benched at 45
    EXPECT_EQ(p.GetMaxOwnedLevel(), 45u);     // benched class still counts
}

TEST(MulticlassProfile, Load_dropsSlotsAboveEffectiveCapWhenUnlockedBinds)
{
    MulticlassProfile p;
    p.SetActiveCeiling(6);
    p.SetUnlockedSlots(1);                    // effective cap 1
    p.Load({ { 1, 10, 0 }, { 8, 10, 0 } }, { 1, 8 });
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 1 }));  // 2nd dropped by unlocked-derived cap
    EXPECT_TRUE(p.HasOwnedClass(8));          // benched, still owned
}

TEST(MulticlassProfile, SetSlot_requiresOwnedAndFillsPositions)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);
    p.AddOwnedClass(8);
    EXPECT_FALSE(p.SetSlot(0, 4));          // rogue not owned
    EXPECT_TRUE(p.SetSlot(0, 1));
    EXPECT_TRUE(p.SetSlot(1, 8));
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 1, 8 }));
    EXPECT_EQ(p.GetClassAtSlot(0), 1u);
    EXPECT_EQ(p.GetClassAtSlot(1), 8u);
    EXPECT_TRUE(p.HasActiveClass(8));
    EXPECT_FALSE(p.HasActiveClass(11));
}

TEST(MulticlassProfile, GetProjectedClass_isFirstFilledSlotOrZeroWhenNone)
{
    MulticlassProfile p;
    EXPECT_EQ(p.GetProjectedClass(), 0u);   // nothing active
    p.AddOwnedClass(8);
    p.AddOwnedClass(1);
    p.SetSlot(0, 8);
    p.SetSlot(1, 1);
    EXPECT_EQ(p.GetProjectedClass(), 8u);   // slot-0 class
}

TEST(MulticlassProfile, SetSlot_fillsArbitrarySlotLeavingLowerSlotsEmpty)
{
    MulticlassProfile p;
    p.AddOwnedClass(8);
    EXPECT_TRUE(p.SetSlot(2, 8));           // slot 2 filled; slots 0 and 1 stay empty
    EXPECT_EQ(p.GetClassAtSlot(0), 0u);
    EXPECT_EQ(p.GetClassAtSlot(1), 0u);
    EXPECT_EQ(p.GetClassAtSlot(2), 8u);
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 8 }));  // compact view skips the holes
    EXPECT_EQ(p.GetProjectedClass(), 8u);   // projection = lowest filled slot
}

TEST(MulticlassProfile, GetActiveClassMask_orsFilledSlotBitsOnly)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);                     // Warrior -> bit 0
    p.AddOwnedClass(8);                     // Mage    -> bit 7
    p.AddOwnedClass(11);                    // Druid   -> bit 10 (owned but benched)
    p.SetSlot(0, 1);
    p.SetSlot(1, 8);
    EXPECT_EQ(p.GetActiveClassMask(), (1u << 0) | (1u << 7));  // Druid not active -> excluded
}

TEST(MulticlassProfile, SetSlot_rejectsSlotBeyondCap)
{
    MulticlassProfile p;
    p.SetActiveCeiling(2);
    p.AddOwnedClass(1);
    p.AddOwnedClass(8);
    p.AddOwnedClass(11);
    EXPECT_TRUE(p.SetSlot(0, 1));
    EXPECT_TRUE(p.SetSlot(1, 8));
    EXPECT_FALSE(p.SetSlot(2, 11));         // slot index >= cap
    EXPECT_EQ(p.GetActiveClasses().size(), 2u);
}

TEST(MulticlassProfile, ClearSlot_emptiesPositionWithoutShiftingAndNeverEmptiesAll)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);
    p.AddOwnedClass(4);
    p.SetSlot(0, 1);
    p.SetSlot(1, 4);
    EXPECT_TRUE(p.ClearSlot(0));            // empty slot 0; slot 1 must NOT shift down
    EXPECT_EQ(p.GetClassAtSlot(0), 0u);     // slot 0 is now a hole
    EXPECT_EQ(p.GetClassAtSlot(1), 4u);     // rogue stays put in slot 1
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 4 }));
    EXPECT_EQ(p.GetProjectedClass(), 4u);   // projection = lowest filled slot
    EXPECT_TRUE(p.HasOwnedClass(1));        // benched, still owned
    EXPECT_FALSE(p.ClearSlot(1));           // would empty the active set -> refused
    EXPECT_FALSE(p.ClearSlot(0));           // already empty -> refused
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 4 }));
}

TEST(MulticlassProfile, ClearSlot_thenSetSlot_doesNotClobberOtherSlots)
{
    // Regression: unset slot 0, then refill slot 0 -> the class in slot 1 must survive (positions are
    // stable, so the rogue does not slide into slot 0 to be overwritten).
    MulticlassProfile p;
    p.AddOwnedClass(1);
    p.AddOwnedClass(4);
    p.AddOwnedClass(8);
    p.SetSlot(0, 1);
    p.SetSlot(1, 4);
    EXPECT_TRUE(p.ClearSlot(0));
    EXPECT_TRUE(p.SetSlot(0, 8));           // refill slot 0 with mage
    EXPECT_EQ(p.GetClassAtSlot(0), 8u);
    EXPECT_EQ(p.GetClassAtSlot(1), 4u);     // rogue survives
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 8, 4 }));
}

TEST(MulticlassProfile, SetSlot_replacesInPlaceKeepsOldOwned)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);                     // Warrior (creation class)
    p.AddOwnedClass(4);                     // Rogue (owned, benched)
    p.SetSlot(0, 1);
    EXPECT_TRUE(p.SetSlot(0, 4));           // become a rogue in the only slot
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 4 }));
    EXPECT_EQ(p.GetProjectedClass(), 4u);
    EXPECT_TRUE(p.HasOwnedClass(1));        // warrior benched, not gone
    EXPECT_FALSE(p.HasActiveClass(1));      // no privileged creation slot
}

TEST(MulticlassProfile, SetSlot_rejectsUnownedInvalidAndOutOfRange)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);
    p.SetSlot(0, 1);
    EXPECT_FALSE(p.SetSlot(0, 8));          // mage not owned
    EXPECT_FALSE(p.SetSlot(0, 10));         // invalid id
    EXPECT_FALSE(p.SetSlot(3, 1));          // slot index >= cap (default 3)
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 1 }));
}

TEST(MulticlassProfile, SetSlot_rejectsDuplicateElsewhereAllowsSelf)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);
    p.AddOwnedClass(8);
    p.SetSlot(0, 1);
    p.SetSlot(1, 8);
    EXPECT_FALSE(p.SetSlot(0, 8));          // 8 already active in slot 1 -> duplicate
    EXPECT_TRUE(p.SetSlot(0, 1));           // setting slot 0 to itself -> no-op, allowed
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 1, 8 }));
}

TEST(MulticlassProfile, Load_resetsThenPopulatesPoolAndActiveInOrder)
{
    MulticlassProfile p;
    p.AddOwnedClass(7);                 // pre-existing state that Load must clear
    p.Load({ { 1, 40, 500 }, { 8, 12, 30 }, { 4, 1, 0 } }, { 8, 1 });
    EXPECT_EQ(p.GetOwnedClasses(), (std::vector<uint8>{ 1, 8, 4 }));
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 8, 1 }));  // order preserved, 4 benched
    EXPECT_EQ(p.GetProjectedClass(), 8u);
    EXPECT_EQ(p.GetClassLevel(1), 40u);
    EXPECT_EQ(p.GetClassXp(8), 30u);
    EXPECT_FALSE(p.HasOwnedClass(7));   // prior state cleared
}

TEST(MulticlassProfile, Load_respectsMaxActiveCap)
{
    MulticlassProfile p;
    p.SetActiveCeiling(1);
    p.Load({ { 1, 1, 0 }, { 8, 1, 0 } }, { 1, 8 });
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 1 }));  // second active dropped by cap
    EXPECT_TRUE(p.HasOwnedClass(8));                             // still owned, just benched
}

TEST(MulticlassProfile, Load_preservesSlotHoles)
{
    // A persisted empty slot 0 (classId 0) with a class in slot 1 must round-trip as a hole, not compact.
    MulticlassProfile p;
    p.Load({ { 1, 10, 0 }, { 4, 10, 0 } }, { 0, 4 });
    EXPECT_EQ(p.GetClassAtSlot(0), 0u);                          // hole preserved
    EXPECT_EQ(p.GetClassAtSlot(1), 4u);
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 4 }));  // compact view = just the rogue
    EXPECT_EQ(p.GetProjectedClass(), 4u);
    EXPECT_TRUE(p.HasOwnedClass(1));                             // owned but in no slot (benched)
}

TEST(MulticlassProfile, CompactSoleActiveIntoCap_relocatesLoneStrandedClassToSlotZero)
{
    MulticlassProfile p;
    p.SetActiveCeiling(3);
    p.AddOwnedClass(8);
    p.SetSlot(2, 8);                 // Mage alone at slot 2 (holes 0,1)
    p.SetActiveCeiling(2);           // cap now 2 -> Mage@2 is stranded above the cap
    EXPECT_TRUE(p.CompactSoleActiveIntoCap());
    EXPECT_EQ(p.GetClassAtSlot(0), 8u);
    EXPECT_EQ(p.GetClassAtSlot(2), 0u);
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 8 }));
    EXPECT_EQ(p.GetProjectedClass(), 8u);
    EXPECT_FALSE(p.CompactSoleActiveIntoCap());   // now in range -> no-op
}

TEST(MulticlassProfile, CompactSoleActiveIntoCap_noopWhenInRangeOrMultiActive)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);
    p.SetSlot(0, 1);
    EXPECT_FALSE(p.CompactSoleActiveIntoCap());   // sole class already at slot 0
    p.AddOwnedClass(8);
    p.SetSlot(1, 8);
    EXPECT_FALSE(p.CompactSoleActiveIntoCap());   // two active -> not sole
}

TEST(MulticlassProfile, HighestActiveSlotAboveCap_detectsOverCapClassDespiteHole)
{
    // The exact live-gate-5 regression: a class stranded above the cap with a hole below it. The filled
    // count (2) equals the cap (2), so the original count-based eviction wrongly concluded "fits" and
    // benched nothing. Position-based detection must flag the over-cap slot index.
    MulticlassProfile p;
    p.SetActiveCeiling(3);
    p.AddOwnedClass(1);   // Warrior
    p.AddOwnedClass(8);   // Mage
    p.SetSlot(0, 1);
    p.SetSlot(2, 8);      // Warrior@0, hole@1, Mage@2
    p.SetActiveCeiling(2);
    EXPECT_EQ(p.HighestActiveSlotAboveCap(), 2);   // Mage@2 is over-cap despite the count fitting the cap
}

TEST(MulticlassProfile, HighestActiveSlotAboveCap_negativeWhenAllWithinCap)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);
    p.AddOwnedClass(8);
    p.SetSlot(0, 1);
    p.SetSlot(1, 8);       // Warrior@0, Mage@1 (cap default 3)
    EXPECT_EQ(p.HighestActiveSlotAboveCap(), -1);
    p.SetActiveCeiling(2);
    EXPECT_EQ(p.HighestActiveSlotAboveCap(), -1);   // both still within cap 2
    p.SetActiveCeiling(1);
    EXPECT_EQ(p.HighestActiveSlotAboveCap(), 1);    // Mage@1 is now over-cap
}

TEST(MulticlassProfile, Load_rescuesLoneOverCapClassIntoSlotZero)
{
    MulticlassProfile p;
    p.SetActiveCeiling(2);
    p.Load({ { 8, 20, 0 } }, { 0, 0, 8 });   // Mage persisted at slot 2, holes 0,1; cap 2
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 8 }));   // rescued, not empty
    EXPECT_EQ(p.GetClassAtSlot(0), 8u);
    EXPECT_EQ(p.GetProjectedClass(), 8u);
}

TEST(MulticlassProfile, GetOwnedClassMask_orsAllOwnedBits)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);                 // bit 0
    p.AddOwnedClass(11);                // bit 10
    EXPECT_EQ(p.GetOwnedClassMask(), (1u << 0) | (1u << 10));
}

TEST(MulticlassProfile, MinLevelQueries_selectLowestActiveClasses)
{
    MulticlassProfile p;
    p.Load({ { 1, 20, 0 }, { 8, 5, 0 }, { 4, 5, 0 }, { 11, 30, 0 } }, { 1, 8, 4 });
    EXPECT_EQ(p.GetMinActiveLevel(), 5u);
    EXPECT_EQ(p.GetActiveClassesAtMinLevel(), (std::vector<uint8>{ 8, 4 }));  // 11 benched, 1 higher
    MulticlassProfile empty;
    EXPECT_EQ(empty.GetMinActiveLevel(), 0u);
    EXPECT_TRUE(empty.GetActiveClassesAtMinLevel().empty());
}
