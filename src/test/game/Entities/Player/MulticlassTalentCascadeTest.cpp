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

// tab 1, all tier 0, no deps: removing one point from a leaf touches only it.
TEST(TalentCascade, LeafRemoval)
{
    std::vector<TalentRecord> t = {
        { 100, 1, 0, 0, 0, 3 },
        { 101, 1, 0, 0, 0, 2 },
    };
    auto r = ComputeTalentRemovalCascade(t, 100, 1);
    EXPECT_EQ(r[0].second, 2u);   // 100: 3 -> 2
    EXPECT_EQ(r[1].second, 2u);   // 101 untouched
}

// prerequisite cascade: B needs A at rank >= 2; dropping A below 2 removes B entirely.
TEST(TalentCascade, PrereqCascade)
{
    std::vector<TalentRecord> t = {
        { 100, 1, 0, 0, 0, 2 },          // A
        { 101, 1, 0, 100, 1, 1 },        // B depends on A, depRank 1 (needs A rank >= 2)
    };
    auto r = ComputeTalentRemovalCascade(t, 100, 1);
    EXPECT_EQ(r[0].second, 1u);   // A: 2 -> 1
    EXPECT_EQ(r[1].second, 0u);   // B removed (prereq no longer met)
}

// tier cascade: C is tier 1 (needs 5 tab points). Removing 2 filler points drops the tab to 4 -> C removed.
TEST(TalentCascade, TierCascade)
{
    std::vector<TalentRecord> t = {
        { 100, 1, 0, 0, 0, 5 },          // filler, tier 0
        { 101, 1, 1, 0, 0, 1 },          // C, tier 1 (gate 5)
    };
    auto r = ComputeTalentRemovalCascade(t, 100, 2);
    EXPECT_EQ(r[0].second, 3u);   // filler 5 -> 3 ; tab now 3(+C) = 4 < 5
    EXPECT_EQ(r[1].second, 0u);   // C removed
}

// full removal of a talent (count >= rank).
TEST(TalentCascade, FullRemoval)
{
    std::vector<TalentRecord> t = { { 100, 1, 0, 0, 0, 2 } };
    auto r = ComputeTalentRemovalCascade(t, 100, 5);
    EXPECT_EQ(r[0].second, 0u);
}

// multi-level fixpoint: dropping the base cascades tier then prereq.
TEST(TalentCascade, MultiLevelFixpoint)
{
    std::vector<TalentRecord> t = {
        { 100, 1, 0, 0,   0, 5 },        // base filler tier 0
        { 101, 1, 1, 0,   0, 5 },        // mid tier 1 (gate 5)
        { 102, 1, 2, 101, 0, 1 },        // top tier 2 (gate 10), depends on mid rank>=1
    };
    // remove 5 from base -> tab 6 < 10 removes top; tab now 5 (>=5) keeps mid.
    auto r = ComputeTalentRemovalCascade(t, 100, 5);
    EXPECT_EQ(r[0].second, 0u);   // base gone
    EXPECT_EQ(r[1].second, 5u);   // mid stays (tab 5 >= 5)
    EXPECT_EQ(r[2].second, 0u);   // top gone (tier gate + prereq)
}
