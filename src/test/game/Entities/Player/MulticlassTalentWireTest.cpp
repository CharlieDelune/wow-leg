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

#include "MulticlassClientProtocol.h"
#include "gtest/gtest.h"

using namespace Multiclass;

TEST(MulticlassTalentWireTest, ParseSpendTalent_valid)
{
    ClientRequest const r = ParseClientRequest("spendtalent 8 1234 2");
    EXPECT_EQ(r.verb, ClientVerb::SpendTalent);
    EXPECT_EQ(r.talentClass, 8u);
    EXPECT_EQ(r.talentId, 1234u);
    EXPECT_EQ(r.talentRank, 2u);
}

TEST(MulticlassTalentWireTest, ParseSpendTalent_wrongArity_isInvalid)
{
    EXPECT_EQ(ParseClientRequest("spendtalent 8 1234").verb, ClientVerb::Invalid);
    EXPECT_EQ(ParseClientRequest("spendtalent 8 1234 2 9").verb, ClientVerb::Invalid);
}

TEST(MulticlassTalentWireTest, ParseSpendTalent_badNumber_isInvalid)
{
    EXPECT_EQ(ParseClientRequest("spendtalent x 1234 2").verb, ClientVerb::Invalid);
    EXPECT_EQ(ParseClientRequest("spendtalent 8 12ab 2").verb, ClientVerb::Invalid);
}

TEST(MulticlassTalentWireTest, ParseResetTalents_valid)
{
    ClientRequest const r = ParseClientRequest("resettalents 11");
    EXPECT_EQ(r.verb, ClientVerb::ResetTalents);
    EXPECT_EQ(r.talentClass, 11u);
}

TEST(MulticlassTalentWireTest, ParseResetTalents_wrongArity_isInvalid)
{
    EXPECT_EQ(ParseClientRequest("resettalents").verb, ClientVerb::Invalid);
    EXPECT_EQ(ParseClientRequest("resettalents 1 2").verb, ClientVerb::Invalid);
}

TEST(MulticlassTalentWireTest, ParseRemoveTalent_valid)
{
    ClientRequest const r = ParseClientRequest("removetalent 8 1953");
    EXPECT_EQ(r.verb, ClientVerb::RemoveTalent);
    EXPECT_EQ(r.talentClass, 8);
    EXPECT_EQ(r.talentId, 1953u);
}

TEST(MulticlassTalentWireTest, ParseRemoveTalent_wrongArity_isInvalid)
{
    EXPECT_EQ(ParseClientRequest("removetalent 8").verb, ClientVerb::Invalid);
    EXPECT_EQ(ParseClientRequest("removetalent 8 1953 2").verb, ClientVerb::Invalid);
}

TEST(MulticlassTalentWireTest, ParseRemoveTalent_badNumber_isInvalid)
{
    EXPECT_EQ(ParseClientRequest("removetalent x 1953").verb, ClientVerb::Invalid);
    EXPECT_EQ(ParseClientRequest("removetalent 8 19ab").verb, ClientVerb::Invalid);
}

TEST(MulticlassTalentWireTest, SerializeClassTalents_formatsRanks)
{
    std::vector<std::pair<uint32, uint32>> ranks = { {1234, 2}, {1235, 1} };
    EXPECT_EQ(SerializeClassTalents(8, 5, ranks), "talents 8 5 1234:2 1235:1");
}

TEST(MulticlassTalentWireTest, SerializeClassTalents_emptyRanks)
{
    EXPECT_EQ(SerializeClassTalents(1, 0, {}), "talents 1 0");
}

TEST(MulticlassTalentWireTest, ParseU32_rejectsOverflowAndJunk)
{
    uint32 v = 0;
    EXPECT_TRUE(ParseU32("4294967295", v));
    EXPECT_EQ(v, 4294967295u);
    EXPECT_FALSE(ParseU32("4294967296", v));
    EXPECT_FALSE(ParseU32("12x", v));
    EXPECT_FALSE(ParseU32("", v));
}
