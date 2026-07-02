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

TEST(MulticlassProfile, Activate_requiresOwnedAndAddsToActiveSetOrdered)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);
    p.AddOwnedClass(8);
    EXPECT_FALSE(p.Activate(4));            // rogue not owned
    EXPECT_TRUE(p.Activate(1));
    EXPECT_TRUE(p.Activate(8));
    EXPECT_FALSE(p.Activate(1));            // already active
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 1, 8 }));
    EXPECT_TRUE(p.HasActiveClass(8));
    EXPECT_FALSE(p.HasActiveClass(11));
}

TEST(MulticlassProfile, GetProjectedClass_isFirstActiveOrZeroWhenNone)
{
    MulticlassProfile p;
    EXPECT_EQ(p.GetProjectedClass(), 0u);  // empty active
    p.AddOwnedClass(8);
    p.AddOwnedClass(1);
    p.Activate(8);
    p.Activate(1);
    EXPECT_EQ(p.GetProjectedClass(), 8u);  // first activated
}

TEST(MulticlassProfile, GetActiveClassMask_orsActiveBitsOnly)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);                     // Warrior -> bit 0
    p.AddOwnedClass(8);                     // Mage    -> bit 7
    p.AddOwnedClass(11);                    // Druid   -> bit 10 (owned but benched)
    p.Activate(1);
    p.Activate(8);
    EXPECT_EQ(p.GetActiveClassMask(), (1u << 0) | (1u << 7));  // Druid not active -> excluded
}

TEST(MulticlassProfile, Activate_enforcesMaxActiveCap)
{
    MulticlassProfile p;
    p.SetMaxActiveClasses(2);
    p.AddOwnedClass(1);
    p.AddOwnedClass(8);
    p.AddOwnedClass(11);
    EXPECT_TRUE(p.Activate(1));
    EXPECT_TRUE(p.Activate(8));
    EXPECT_FALSE(p.Activate(11));          // cap reached
    EXPECT_EQ(p.GetActiveClasses().size(), 2u);
}

TEST(MulticlassProfile, Deactivate_removesButNeverEmptiesActive)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);
    p.AddOwnedClass(8);
    p.Activate(1);
    p.Activate(8);
    EXPECT_TRUE(p.Deactivate(1));
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 8 }));
    EXPECT_TRUE(p.HasOwnedClass(1));       // benched, still owned
    EXPECT_FALSE(p.Deactivate(8));         // would empty the active set -> refused
    EXPECT_FALSE(p.Deactivate(11));        // not active
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 8 }));
}

TEST(MulticlassProfile, ReplaceActiveAt_swapsInPlaceAndKeepsOldOwned)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);                     // Warrior (creation class)
    p.AddOwnedClass(4);                     // Rogue (owned, benched)
    p.Activate(1);
    EXPECT_TRUE(p.ReplaceActiveAt(0, 4));   // become a rogue in the only slot
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 4 }));
    EXPECT_EQ(p.GetProjectedClass(), 4u);
    EXPECT_TRUE(p.HasOwnedClass(1));        // warrior benched, not gone
    EXPECT_FALSE(p.HasActiveClass(1));      // no privileged creation slot
}

TEST(MulticlassProfile, ReplaceActiveAt_rejectsUnownedInvalidAndOutOfRange)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);
    p.Activate(1);
    EXPECT_FALSE(p.ReplaceActiveAt(0, 8));  // mage not owned
    EXPECT_FALSE(p.ReplaceActiveAt(0, 10)); // invalid id
    EXPECT_FALSE(p.ReplaceActiveAt(3, 1));  // index out of range
    EXPECT_EQ(p.GetActiveClasses(), (std::vector<uint8>{ 1 }));
}

TEST(MulticlassProfile, ReplaceActiveAt_rejectsDuplicateElsewhereAllowsSelf)
{
    MulticlassProfile p;
    p.AddOwnedClass(1);
    p.AddOwnedClass(8);
    p.Activate(1);
    p.Activate(8);
    EXPECT_FALSE(p.ReplaceActiveAt(0, 8));  // 8 already active at index 1 -> duplicate
    EXPECT_TRUE(p.ReplaceActiveAt(0, 1));   // replacing index 0 with itself -> no-op, allowed
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
