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
#include <string>
#include <unordered_map>

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

TEST(MulticlassLogicTest, MinActiveLevel_isZeroWhenNoneActiveElseLowest)
{
    EXPECT_EQ(MinActiveLevel(MakeSlots({ 0, 1, 0 })), 0u);                       // none active
    EXPECT_EQ(MinActiveLevel(MakeSlots({ 1, 25, 0 })), 25u);                     // single
    EXPECT_EQ(MinActiveLevel(MakeSlots({ 1, 25, 0 }, { 4, 10, 0 })), 10u);       // two
    EXPECT_EQ(MinActiveLevel(MakeSlots({ 1, 12, 0 }, { 4, 12, 0 }, { 8, 15, 0 })), 12u);
}

TEST(MulticlassLogicTest, SlotsAtMinLevel_selectsEveryActiveClassTiedAtMinimum)
{
    // all three equal -> all three slot indices
    std::vector<uint8> all = SlotsAtMinLevel(MakeSlots({ 1, 12, 0 }, { 4, 12, 0 }, { 8, 12, 0 }));
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0], 0u); EXPECT_EQ(all[1], 1u); EXPECT_EQ(all[2], 2u);

    // one lower -> only that slot
    std::vector<uint8> one = SlotsAtMinLevel(MakeSlots({ 1, 10, 0 }, { 4, 12, 0 }, { 8, 15, 0 }));
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0], 0u);

    // two tied low, one high -> the two low slots
    std::vector<uint8> two = SlotsAtMinLevel(MakeSlots({ 1, 10, 0 }, { 4, 10, 0 }, { 8, 15, 0 }));
    ASSERT_EQ(two.size(), 2u);
    EXPECT_EQ(two[0], 0u); EXPECT_EQ(two[1], 1u);

    // none active -> empty
    EXPECT_TRUE(SlotsAtMinLevel(MakeSlots({ 0, 1, 0 })).empty());
}

namespace
{
    // Flat synthetic curve: every level needs `per` XP to advance.
    struct FlatCurve
    {
        uint32 per;
        uint32 operator()(uint8 /*level*/) const { return per; }
    };
}

TEST(MulticlassLogicTest, ApplyXpToClass_accruesWithoutLevelingBelowThreshold)
{
    ClassProgress cp{ 8, 1, 0 };
    EXPECT_EQ(ApplyXpToClass(cp, 50u, 80, FlatCurve{ 100u }), 0u);
    EXPECT_EQ(cp.level, 1u);
    EXPECT_EQ(cp.xp, 50u);
}

TEST(MulticlassLogicTest, ApplyXpToClass_singleLevelKeepsRemainder)
{
    ClassProgress cp{ 8, 1, 0 };
    EXPECT_EQ(ApplyXpToClass(cp, 120u, 80, FlatCurve{ 100u }), 1u);
    EXPECT_EQ(cp.level, 2u);
    EXPECT_EQ(cp.xp, 20u);
}

TEST(MulticlassLogicTest, ApplyXpToClass_multiLevelInOneAward)
{
    ClassProgress cp{ 8, 1, 0 };
    EXPECT_EQ(ApplyXpToClass(cp, 350u, 80, FlatCurve{ 100u }), 3u);  // 100+100+100 used, 50 left
    EXPECT_EQ(cp.level, 4u);
    EXPECT_EQ(cp.xp, 50u);
}

TEST(MulticlassLogicTest, ApplyXpToClass_capsAtMaxLevelWithNoOverflowXp)
{
    ClassProgress cp{ 8, 79, 0 };
    EXPECT_EQ(ApplyXpToClass(cp, 10000u, 80, FlatCurve{ 100u }), 1u);
    EXPECT_EQ(cp.level, 80u);
    EXPECT_EQ(cp.xp, 0u);                                           // overflow discarded at cap
}

TEST(MulticlassLogicTest, ApplyXpToClass_atMaxLevelIsNoOp)
{
    ClassProgress cp{ 8, 80, 0 };
    EXPECT_EQ(ApplyXpToClass(cp, 500u, 80, FlatCurve{ 100u }), 0u);
    EXPECT_EQ(cp.level, 80u);
    EXPECT_EQ(cp.xp, 0u);
}

TEST(MulticlassLogicTest, CatchUp_twoTiedLowClassesEachGainFullAwardAndCanInvert)
{
    // Snapshot-at-award: the two level-10 classes each get the FULL award and may
    // overshoot the level-15 class; the third is untouched (gains nothing this award).
    SlotArray slots = MakeSlots({ 1, 10, 0 }, { 4, 10, 0 }, { 8, 15, 0 });
    FlatCurve curve{ 100u };
    for (uint8 idx : SlotsAtMinLevel(slots))
        ApplyXpToClass(slots[idx], 650u, 80, curve);               // 650 -> +6 levels each
    EXPECT_EQ(slots[0].level, 16u);
    EXPECT_EQ(slots[1].level, 16u);
    EXPECT_EQ(slots[2].level, 15u);                                // never received this award
    EXPECT_EQ(MinActiveLevel(slots), 15u);                         // now the third is lowest
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

TEST(MulticlassLogicTest, ActiveClassMask_orsActiveClassBits)
{
    EXPECT_EQ(ActiveClassMask(MakeSlots({ 0, 1, 0 })), 0u);                              // none active
    EXPECT_EQ(ActiveClassMask(MakeSlots({ 1, 40, 0 })), 1u << 0);                        // warrior
    EXPECT_EQ(ActiveClassMask(MakeSlots({ 1, 40, 0 }, { 8, 2, 0 })), (1u << 0) | (1u << 7)); // warrior + mage
    EXPECT_EQ(ActiveClassMask(MakeSlots({ 8, 2, 0 }, { 4, 10, 0 }, { 5, 3, 0 })),
              (1u << 7) | (1u << 3) | (1u << 4));                                         // mage + rogue + priest
}

TEST(MulticlassLogicTest, ClassLevel_returnsSlotLevelOrZero)
{
    SlotArray slots = MakeSlots({ 1, 40, 0 }, { 8, 2, 0 });   // warrior 40, mage 2
    EXPECT_EQ(ClassLevel(slots, 1), 40u);   // warrior slot level
    EXPECT_EQ(ClassLevel(slots, 8), 2u);    // mage slot level
    EXPECT_EQ(ClassLevel(slots, 4), 0u);    // rogue not active
    EXPECT_EQ(ClassLevel(slots, 0), 0u);    // empty/invalid classId
}

TEST(MulticlassUnlocked, MaskIsUnionOfPool)
{
    std::unordered_map<uint8, Multiclass::ClassProgress> pool;
    pool[1] = {1, 40, 0};   // Warrior
    pool[8] = {8, 12, 0};   // Mage
    // Warrior bit0 (1) | Mage bit7 (128) = 129
    EXPECT_EQ(Multiclass::UnlockedClassMask(pool), 129u);
    EXPECT_EQ(Multiclass::UnlockedClassMask({}), 0u);
}

TEST(MulticlassUnlocked, LevelLookup)
{
    std::unordered_map<uint8, Multiclass::ClassProgress> pool;
    pool[8] = {8, 27, 500};
    EXPECT_EQ(Multiclass::UnlockedClassLevel(pool, 8), 27);
    EXPECT_EQ(Multiclass::UnlockedClassLevel(pool, 4), 0);  // not unlocked
}

TEST(MulticlassUnlocked, ClaimingRangesOverWholePoolDeterministically)
{
    std::unordered_map<uint8, Multiclass::ClassProgress> pool;
    pool[8] = {8, 5, 0};    // Mage
    pool[1] = {1, 5, 0};    // Warrior
    // combined mask with Warrior(1)|Mage(128) -> both, ascending classId order
    std::vector<uint8> got = Multiclass::ClaimingUnlockedClasses(pool, 129u);
    EXPECT_EQ(got, (std::vector<uint8>{1, 8}));
    EXPECT_TRUE(Multiclass::ClaimingUnlockedClasses(pool, 0u).empty());  // no class-specific entry
}

TEST(MulticlassCombineValues, HighestReturnsMaxAcrossClasses)
{
    EXPECT_FLOAT_EQ(CombineValues(MULTICLASS_STATS_HIGHEST, { 10.0f, 25.0f, 18.0f }), 25.0f);
}

TEST(MulticlassCombineValues, SumAddsEveryClass)
{
    EXPECT_FLOAT_EQ(CombineValues(MULTICLASS_STATS_SUM, { 10.0f, 25.0f, 18.0f }), 53.0f);
}

TEST(MulticlassCombineValues, SingleElementIsIdentityInBothModes)
{
    EXPECT_FLOAT_EQ(CombineValues(MULTICLASS_STATS_HIGHEST, { 42.0f }), 42.0f);   // byte-vanilla invariant
    EXPECT_FLOAT_EQ(CombineValues(MULTICLASS_STATS_SUM, { 42.0f }), 42.0f);
}

TEST(MulticlassCombineValues, EmptyIsNeutralZeroInBothModes)
{
    EXPECT_FLOAT_EQ(CombineValues(MULTICLASS_STATS_HIGHEST, {}), 0.0f);
    EXPECT_FLOAT_EQ(CombineValues(MULTICLASS_STATS_SUM, {}), 0.0f);
}

TEST(MulticlassCombineValues, HighestPicksLeastNegativeNotAZeroFloor)
{
    // Attack-power formulas go negative at low stats (INT classes: STR - 10). Max must return the
    // least-negative contribution, never clamp to 0.
    EXPECT_FLOAT_EQ(CombineValues(MULTICLASS_STATS_HIGHEST, { -8.0f, -3.0f }), -3.0f);
}

TEST(MulticlassLogicTest, TalentResetCost_openingTiersClimbCapDecayFloor)
{
    constexpr uint32 G = 10000;             // copper per gold (SharedDefines GOLD)
    constexpr uint32 DAY_S = 86400;
    constexpr uint32 P = 30 * DAY_S;        // native decay window

    // Opening tiers key off the last cost paid, independent of elapsed time.
    EXPECT_EQ(TalentResetCost(0 * G, 0, P), 1 * G);     // never reset -> 1g
    EXPECT_EQ(TalentResetCost(1 * G, 0, P), 5 * G);     // last 1g   -> 5g
    EXPECT_EQ(TalentResetCost(5 * G, 0, P), 10 * G);    // last 5g   -> 10g

    // Past 10g with no full period elapsed: climb +5g.
    EXPECT_EQ(TalentResetCost(10 * G, 0, P), 15 * G);
    EXPECT_EQ(TalentResetCost(45 * G, 0, P), 50 * G);   // climb hits the cap
    EXPECT_EQ(TalentResetCost(50 * G, 0, P), 50 * G);   // stays capped at 50g

    // Decay by whole periods elapsed: 5g per period.
    EXPECT_EQ(TalentResetCost(30 * G, 2 * P, P), 20 * G);   // 30g - 2*5g
    EXPECT_EQ(TalentResetCost(15 * G, 10 * P, P), 10 * G);  // floored at 10g
}

TEST(MulticlassLogicTest, TalentResetCost_decayPeriodIsTunable)
{
    constexpr uint32 G = 10000;
    constexpr uint32 DAY_S = 86400;

    // 7 days elapsed: at the native 30-day window no period has passed -> climb.
    EXPECT_EQ(TalentResetCost(30 * G, 7 * DAY_S, 30 * DAY_S), 35 * G);
    // Same 7 days elapsed at a 7-day window -> exactly one decay step.
    EXPECT_EQ(TalentResetCost(30 * G, 7 * DAY_S, 7 * DAY_S), 25 * G);
}

TEST(MulticlassLogicTest, GlyphSlotUnlockLevel_matchesRetailLadder)
{
    EXPECT_EQ(GlyphSlotUnlockLevel(0), 15u);
    EXPECT_EQ(GlyphSlotUnlockLevel(1), 15u);
    EXPECT_EQ(GlyphSlotUnlockLevel(2), 50u);
    EXPECT_EQ(GlyphSlotUnlockLevel(3), 30u);
    EXPECT_EQ(GlyphSlotUnlockLevel(4), 70u);
    EXPECT_EQ(GlyphSlotUnlockLevel(5), 80u);
    EXPECT_EQ(GlyphSlotUnlockLevel(6), 0u);   // out of range -> no gate
}

TEST(MulticlassLogicTest, GlyphEnabledSlotMask_matchesNativeBitmask)
{
    EXPECT_EQ(GlyphEnabledSlotMask(1, 6),  0x00u);
    EXPECT_EQ(GlyphEnabledSlotMask(14, 6), 0x00u);
    EXPECT_EQ(GlyphEnabledSlotMask(15, 6), 0x03u);   // slots 0,1
    EXPECT_EQ(GlyphEnabledSlotMask(30, 6), 0x0Bu);   // + slot 3 (0x08)
    EXPECT_EQ(GlyphEnabledSlotMask(50, 6), 0x0Fu);   // + slot 2 (0x04)
    EXPECT_EQ(GlyphEnabledSlotMask(70, 6), 0x1Fu);   // + slot 4 (0x10)
    EXPECT_EQ(GlyphEnabledSlotMask(80, 6), 0x3Fu);   // + slot 5 (0x20)
}

TEST(MulticlassLogicTest, ClassIdFromMask_decodesSingleClassBit)
{
    EXPECT_EQ(ClassIdFromMask(0u), 0u);                        // no class
    EXPECT_EQ(ClassIdFromMask(1u << (1 - 1)), 1u);             // warrior
    EXPECT_EQ(ClassIdFromMask(1u << (8 - 1)), 8u);             // mage
    EXPECT_EQ(ClassIdFromMask(1u << (11 - 1)), 11u);           // druid
    EXPECT_EQ(ClassIdFromMask((1u << (1 - 1)) | (1u << (8 - 1))), 1u);  // multi-bit -> lowest set
}

TEST(MulticlassLogicTest, DeclassifyText_upperTokenCapitalizes)
{
    std::string t = "Well met, $C.";
    DeclassifyText(t, "adventurer");
    EXPECT_EQ(t, "Well met, Adventurer.");
}

TEST(MulticlassLogicTest, DeclassifyText_lowerTokenVerbatim)
{
    std::string t = "a brave $c indeed";
    DeclassifyText(t, "adventurer");
    EXPECT_EQ(t, "a brave adventurer indeed");
}

TEST(MulticlassLogicTest, DeclassifyText_multipleAndAdjacentTokens)
{
    std::string t = "$C, $c, $C$c";
    DeclassifyText(t, "hero");
    EXPECT_EQ(t, "Hero, hero, Herohero");
}

TEST(MulticlassLogicTest, DeclassifyText_multiWordCapitalizesFirstOnly)
{
    std::string t = "$C";
    DeclassifyText(t, "wandering hero");
    EXPECT_EQ(t, "Wandering hero");
}

TEST(MulticlassLogicTest, DeclassifyText_leavesOtherTokensAndPlainText)
{
    std::string t = "$N the $R does $B things 100% $$";
    DeclassifyText(t, "adventurer");
    EXPECT_EQ(t, "$N the $R does $B things 100% $$");
}

TEST(MulticlassLogicTest, DeclassifyText_emptyWordIsNoOp)
{
    std::string t = "$C and $c";
    DeclassifyText(t, "");
    EXPECT_EQ(t, "$C and $c");
}

TEST(MulticlassLogicTest, DeclassifyText_trailingDollarSafe)
{
    std::string t = "ends with $";
    DeclassifyText(t, "adventurer");
    EXPECT_EQ(t, "ends with $");
}

TEST(MulticlassLogicTest, DeclassifyText_escapedDollarBeforeClassLetterPreserved)
{
    std::string t = "cost $$C here";
    DeclassifyText(t, "adventurer");
    EXPECT_EQ(t, "cost $$C here");
}

TEST(MulticlassLogicTest, ReplaceClassToken_upperAndLowerUseDistinctReplacements)
{
    std::string t = "$C fights, the $c flees";
    ReplaceClassToken(t, "{mcU}", "{mcL}");
    EXPECT_EQ(t, "{mcU} fights, the {mcL} flees");
}

TEST(MulticlassLogicTest, ReplaceClassToken_leavesOtherTokensEscapesAndPlainText)
{
    std::string t = "$N the $R does $$C things 100% $$";
    ReplaceClassToken(t, "{mcU}", "{mcL}");
    EXPECT_EQ(t, "$N the $R does $$C things 100% $$");
}

TEST(MulticlassLogicTest, ReplaceClassToken_noDollarIsNoOp)
{
    std::string t = "Jerry has gone rogue";
    ReplaceClassToken(t, "{mcU}", "{mcL}");
    EXPECT_EQ(t, "Jerry has gone rogue");
}
