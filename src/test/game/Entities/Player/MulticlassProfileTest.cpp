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

TEST(MulticlassProfile, MaxActiveClasses_defaultsToThreeAndClampsToOne)
{
    MulticlassProfile p;
    EXPECT_EQ(p.GetMaxActiveClasses(), 3u);
    p.SetMaxActiveClasses(0);
    EXPECT_EQ(p.GetMaxActiveClasses(), 1u);  // clamped
    p.SetMaxActiveClasses(9);
    EXPECT_EQ(p.GetMaxActiveClasses(), 9u);
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
    p.SetMaxActiveClasses(2);
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
    p.SetMaxActiveClasses(1);
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
