/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "MulticlassLogic.h"
#include "gtest/gtest.h"

using namespace Multiclass;

namespace
{
    SlotArray MakeSlots(ClassProgress a, ClassProgress b = {}, ClassProgress c = {})
    {
        return SlotArray{ a, b, c };
    }
}

TEST(MulticlassLogicTest, ActiveCount_countsNonEmptySlots)
{
    EXPECT_EQ(ActiveCount(MakeSlots({ 1, 25, 0 })), 1u);
    EXPECT_EQ(ActiveCount(MakeSlots({ 1, 25, 0 }, { 4, 10, 0 })), 2u);
    EXPECT_EQ(ActiveCount(MakeSlots({ 1, 25, 0 }, { 4, 10, 0 }, { 8, 18, 0 })), 3u);
}

TEST(MulticlassLogicTest, ComputeDisplayLevel_isLowestActiveLevel)
{
    EXPECT_EQ(ComputeDisplayLevel(MakeSlots({ 1, 25, 0 })), 25);
    EXPECT_EQ(ComputeDisplayLevel(MakeSlots({ 1, 25, 0 }, { 4, 10, 0 })), 10);
    EXPECT_EQ(ComputeDisplayLevel(MakeSlots({ 1, 25, 0 }, { 4, 10, 0 }, { 8, 18, 0 })), 10);
}

TEST(MulticlassLogicTest, PerClassXp_dividedToFullRange)
{
    EXPECT_EQ(PerClassXp(300, 1, 0.0f), 300u);   // single class always full
    EXPECT_EQ(PerClassXp(300, 3, 0.0f), 100u);   // fully divided
    EXPECT_EQ(PerClassXp(300, 3, 1.0f), 300u);   // full to each
    EXPECT_EQ(PerClassXp(300, 3, 0.5f), 200u);   // halfway: 100 + 0.5*(300-100)
}

TEST(MulticlassLogicTest, TalentPointsForLevel_firstPointAtTen)
{
    EXPECT_EQ(TalentPointsForLevel(9), 0u);
    EXPECT_EQ(TalentPointsForLevel(10), 1u);
    EXPECT_EQ(TalentPointsForLevel(80), 71u);
}

TEST(MulticlassLogicTest, IsValidClassId_excludesGapAtTen)
{
    EXPECT_TRUE(IsValidClassId(1));    // warrior
    EXPECT_TRUE(IsValidClassId(11));   // druid
    EXPECT_FALSE(IsValidClassId(0));
    EXPECT_FALSE(IsValidClassId(10));  // unused in 3.3.5a
    EXPECT_FALSE(IsValidClassId(12));
}

TEST(MulticlassLogicTest, CanAssignClass_rejectsDuplicatesAndInvalid)
{
    SlotArray slots = MakeSlots({ 1, 25, 0 }, { 4, 10, 0 });
    EXPECT_FALSE(CanAssignClass(slots, 2, 1));   // warrior already in slot 0
    EXPECT_FALSE(CanAssignClass(slots, 2, 10));  // invalid class
    EXPECT_FALSE(CanAssignClass(slots, 3, 8));   // slot out of range
    EXPECT_TRUE(CanAssignClass(slots, 2, 8));    // mage into empty slot 2
    EXPECT_TRUE(CanAssignClass(slots, 0, 5));    // replacing own slot with a new class
}

TEST(MulticlassLogicTest, ClaimingClasses_maskZeroIsOwnedByNobody)
{
    SlotArray slots = MakeSlots({ 1, 25, 0 }, { 8, 10, 0 });  // warrior + mage active
    EXPECT_TRUE(ClaimingClasses(slots, 0u).empty());          // general/profession/racial
}

TEST(MulticlassLogicTest, ClaimingClasses_singleClassSpell)
{
    SlotArray slots = MakeSlots({ 1, 25, 0 }, { 8, 10, 0 });  // warrior(1) + mage(8)
    uint32 const mageMask = 1u << (8 - 1);
    std::vector<uint8> owners = ClaimingClasses(slots, mageMask);
    ASSERT_EQ(owners.size(), 1u);
    EXPECT_EQ(owners[0], 8);
}

TEST(MulticlassLogicTest, ClaimingClasses_sharedSpellOwnedByEachActiveMatch)
{
    SlotArray slots = MakeSlots({ 1, 25, 0 }, { 4, 10, 0 }, { 8, 18, 0 });  // warrior + rogue + mage
    uint32 const warriorRogueMask = (1u << (1 - 1)) | (1u << (4 - 1));
    std::vector<uint8> owners = ClaimingClasses(slots, warriorRogueMask);
    ASSERT_EQ(owners.size(), 2u);
    EXPECT_EQ(owners[0], 1);
    EXPECT_EQ(owners[1], 4);
}

TEST(MulticlassLogicTest, ClaimingClasses_inactiveMaskedClassNotClaimed)
{
    SlotArray slots = MakeSlots({ 1, 25, 0 });  // only warrior active
    uint32 const priestMask = 1u << (5 - 1);    // priest not in any slot
    EXPECT_TRUE(ClaimingClasses(slots, priestMask).empty());
}

TEST(MulticlassLogicTest, AnotherActiveClassOwns_detectsSharedOwnership)
{
    std::vector<std::vector<uint32>> others = { { 100u, 200u }, { 300u } };
    EXPECT_TRUE(AnotherActiveClassOwns(200u, others));   // owned by another active class
    EXPECT_FALSE(AnotherActiveClassOwns(999u, others));  // owned by nobody else -> safe to remove
}

TEST(MulticlassLogicTest, AnotherActiveClassOwns_emptyIsSafeToRemove)
{
    std::vector<std::vector<uint32>> none;
    EXPECT_FALSE(AnotherActiveClassOwns(100u, none));
}
