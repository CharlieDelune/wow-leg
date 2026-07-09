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

static std::vector<LoadoutMeta> mk(std::vector<uint32> ids)
{
    std::vector<LoadoutMeta> v;
    for (uint32 id : ids)
        v.push_back({ id, "L", "", "", id });
    return v;
}

TEST(LoadoutLogic, NextIdIsMaxPlusOne)
{
    EXPECT_EQ(NextLoadoutId({}), 1u);
    EXPECT_EQ(NextLoadoutId(mk({1, 2, 5})), 6u);   // gaps don't matter; never reuse
}

TEST(LoadoutLogic, CanCreateRespectsCapacity)
{
    EXPECT_TRUE(CanCreateLoadout(1, 2));
    EXPECT_FALSE(CanCreateLoadout(2, 2));
    EXPECT_FALSE(CanCreateLoadout(3, 2));
}

TEST(LoadoutLogic, DeleteLastIsRejected)
{
    auto r = ResolveDeleteTarget(mk({7}), 7, 7);
    EXPECT_FALSE(r.allowed);
}

TEST(LoadoutLogic, DeleteInactiveKeepsActive)
{
    auto r = ResolveDeleteTarget(mk({1, 2, 3}), 2, 1);
    EXPECT_TRUE(r.allowed);
    EXPECT_EQ(r.newActiveId, 1u);   // active unchanged
}

TEST(LoadoutLogic, DeleteActivePicksAnother)
{
    auto r = ResolveDeleteTarget(mk({1, 2, 3}), 1, 1);
    EXPECT_TRUE(r.allowed);
    EXPECT_NE(r.newActiveId, 1u);   // switched off the deleted one
    EXPECT_TRUE(r.newActiveId == 2u || r.newActiveId == 3u);
}

TEST(LoadoutLogic, DeleteUnknownIdRejected)
{
    EXPECT_FALSE(ResolveDeleteTarget(mk({1, 2}), 9, 1).allowed);
}
