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

#include "MulticlassClassPolicy.h"
#include "gtest/gtest.h"

TEST(MulticlassClassPolicy, ProjectedOnlyContexts)
{
    EXPECT_TRUE(MulticlassIsProjectedOnlyContext(CLASS_CONTEXT_INIT));
    EXPECT_TRUE(MulticlassIsProjectedOnlyContext(CLASS_CONTEXT_TELEPORT));
    EXPECT_TRUE(MulticlassIsProjectedOnlyContext(CLASS_CONTEXT_TALENT_POINT_CALC));
}

TEST(MulticlassClassPolicy, AnyActiveContexts)
{
    for (ClassContext ctx : { CLASS_CONTEXT_NONE, CLASS_CONTEXT_QUEST, CLASS_CONTEXT_SKILL,
                              CLASS_CONTEXT_STATS,  // stat restructure is P2; the boolean gate is any-active now
                              CLASS_CONTEXT_ABILITY, CLASS_CONTEXT_ABILITY_REACTIVE, CLASS_CONTEXT_PET,
                              CLASS_CONTEXT_PET_CHARM, CLASS_CONTEXT_EQUIP_RELIC, CLASS_CONTEXT_EQUIP_SHIELDS,
                              CLASS_CONTEXT_EQUIP_ARMOR_CLASS, CLASS_CONTEXT_WEAPON_SWAP, CLASS_CONTEXT_TAXI,
                              CLASS_CONTEXT_GRAVEYARD, CLASS_CONTEXT_CLASS_TRAINER })
        EXPECT_FALSE(MulticlassIsProjectedOnlyContext(ctx)) << "context " << uint32(ctx);
}
