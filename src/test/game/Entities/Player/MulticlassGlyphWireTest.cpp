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

TEST(MulticlassGlyphWireTest, ParseSocketGlyph_valid)
{
    ClientRequest const r = ParseClientRequest("socketglyph 4 3 41500");
    EXPECT_EQ(r.verb, ClientVerb::SocketGlyph);
    EXPECT_EQ(r.glyphClass, 4u);
    EXPECT_EQ(r.glyphSlot, 3u);
    EXPECT_EQ(r.glyphItemId, 41500u);
}

TEST(MulticlassGlyphWireTest, ParseSocketGlyph_wrongArity_isInvalid)
{
    EXPECT_EQ(ParseClientRequest("socketglyph 4 3").verb, ClientVerb::Invalid);
    EXPECT_EQ(ParseClientRequest("socketglyph 4 3 41500 9").verb, ClientVerb::Invalid);
}

TEST(MulticlassGlyphWireTest, ParseSocketGlyph_badNumber_isInvalid)
{
    EXPECT_EQ(ParseClientRequest("socketglyph x 3 41500").verb, ClientVerb::Invalid);
    EXPECT_EQ(ParseClientRequest("socketglyph 4 3 4x5").verb, ClientVerb::Invalid);
}

TEST(MulticlassGlyphWireTest, ParseRemoveGlyph_valid)
{
    ClientRequest const r = ParseClientRequest("removeglyph 11 0");
    EXPECT_EQ(r.verb, ClientVerb::RemoveGlyph);
    EXPECT_EQ(r.glyphClass, 11u);
    EXPECT_EQ(r.glyphSlot, 0u);
}

TEST(MulticlassGlyphWireTest, ParseRemoveGlyph_wrongArity_isInvalid)
{
    EXPECT_EQ(ParseClientRequest("removeglyph 11").verb, ClientVerb::Invalid);
    EXPECT_EQ(ParseClientRequest("removeglyph 11 0 5").verb, ClientVerb::Invalid);
}

TEST(MulticlassGlyphWireTest, SerializeClassGlyphs_formatsSlots)
{
    std::vector<std::pair<uint32, uint32>> slots = {
        {0, 1}, {0, 1}, {0, 0}, {45789, 1}, {0, 0}, {0, 0}
    };
    EXPECT_EQ(SerializeClassGlyphs(4, slots),
              "glyphs 4 0:0:1 1:0:1 2:0:0 3:45789:1 4:0:0 5:0:0");
}

TEST(MulticlassGlyphWireTest, SerializeClassGlyphs_emptyIsClassOnly)
{
    EXPECT_EQ(SerializeClassGlyphs(1, {}), "glyphs 1");
}
