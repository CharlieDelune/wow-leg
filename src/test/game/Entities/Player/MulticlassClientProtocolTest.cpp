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
#include "MulticlassProfile.h"
#include "gtest/gtest.h"

using namespace Multiclass;

namespace
{
    // Warrior(1) L80 in slot 0, Mage(8) L80 in slot 1, Rogue(4) L34 owned+benched. Cap 3.
    MulticlassProfile MakeProfile()
    {
        MulticlassProfile p;
        p.SetActiveCeiling(3);
        p.SetUnlockedSlots(3);
        p.AddOwnedClass(1, 80, 0);
        p.AddOwnedClass(8, 80, 0);
        p.AddOwnedClass(4, 34, 0);
        p.SetSlot(0, 1);
        p.SetSlot(1, 8);
        return p;
    }
}

TEST(MulticlassClientProtocolTest, ParseSetOrder)
{
    ClientRequest r = ParseClientRequest("setorder 1 8 4");
    EXPECT_EQ(r.verb, ClientVerb::SetOrder);
    ASSERT_EQ(r.order.size(), 3u);
    EXPECT_EQ(r.order[0], 1u);
    EXPECT_EQ(r.order[1], 8u);
    EXPECT_EQ(r.order[2], 4u);
}

TEST(MulticlassClientProtocolTest, ParseHello)
{
    EXPECT_EQ(ParseClientRequest("hello").verb, ClientVerb::Hello);
}

TEST(MulticlassClientProtocolTest, ParseGarbageIsInvalid)
{
    EXPECT_EQ(ParseClientRequest("").verb, ClientVerb::Invalid);
    EXPECT_EQ(ParseClientRequest("setorder").verb, ClientVerb::Invalid);        // no classIds
    EXPECT_EQ(ParseClientRequest("setorder 1 x").verb, ClientVerb::Invalid);    // non-numeric member
    EXPECT_EQ(ParseClientRequest("frobnicate 1 2").verb, ClientVerb::Invalid);
}

TEST(MulticlassClientProtocolTest, ValidateSetOrder_happyPath)
{
    MulticlassProfile p = MakeProfile();
    EXPECT_EQ(ValidateSetOrderRequest(p, {1, 8, 4}, /*inCombat*/ false, /*enabled*/ true), DenyReason::Ok);
    EXPECT_EQ(ValidateSetOrderRequest(p, {8, 1}, false, true), DenyReason::Ok);   // reorder, bench Rogue
    EXPECT_EQ(ValidateSetOrderRequest(p, {4}, false, true), DenyReason::Ok);      // down to a single class
}

TEST(MulticlassClientProtocolTest, ValidateSetOrder_rejections)
{
    MulticlassProfile p = MakeProfile();
    EXPECT_EQ(ValidateSetOrderRequest(p, {1}, false, /*enabled*/ false), DenyReason::FeatureOff);
    EXPECT_EQ(ValidateSetOrderRequest(p, {1}, /*inCombat*/ true, true), DenyReason::InCombat);
    EXPECT_EQ(ValidateSetOrderRequest(p, {}, false, true), DenyReason::NoneActive);        // empty set refused
    EXPECT_EQ(ValidateSetOrderRequest(p, {1, 8, 4, 2}, false, true), DenyReason::TooMany); // 4 > cap(3)
    EXPECT_EQ(ValidateSetOrderRequest(p, {1, 10}, false, true), DenyReason::InvalidClass); // 10 is not a class
    EXPECT_EQ(ValidateSetOrderRequest(p, {1, 5}, false, true), DenyReason::NotOwned);      // Priest not owned
    EXPECT_EQ(ValidateSetOrderRequest(p, {1, 1}, false, true), DenyReason::Duplicate);     // Warrior twice
}

TEST(MulticlassClientProtocolTest, SerializeSnapshot)
{
    MulticlassProfile p = MakeProfile();
    // "state 1 <en> <cap> <proj>" then owned tuples id:level:slot (benched = -1), owned order.
    // Projected = slot-0 class = 1. Owned = {1,8,4} (GetOwnedClasses order).
    std::string s = SerializeStateSnapshot(p, /*enabled*/ true);
    EXPECT_EQ(s, "state 1 1 3 1 1:80:0 8:80:1 4:34:-1");
}

TEST(MulticlassClientProtocolTest, SerializeSnapshot_disabled)
{
    MulticlassProfile p = MakeProfile();
    EXPECT_EQ(SerializeStateSnapshot(p, /*enabled*/ false).substr(0, 9), "state 1 0");
}

TEST(MulticlassClientProtocolTest, ParseWhois)
{
    ClientRequest r = ParseClientRequest("whois Alice Bob");
    EXPECT_EQ(r.verb, ClientVerb::Whois);
    ASSERT_EQ(r.names.size(), 2u);
    EXPECT_EQ(r.names[0], "Alice");
    EXPECT_EQ(r.names[1], "Bob");
    EXPECT_EQ(ParseClientRequest("whois").verb, ClientVerb::Invalid);   // needs >= 1 name
}

TEST(MulticlassClientProtocolTest, SerializePeer)
{
    EXPECT_EQ(SerializePeer("Alice", {1, 8, 11}), "peer Alice 1 8 11");
    EXPECT_EQ(SerializePeer("Bob", {}), "peer Bob");   // unknown/offline -> empty id list
}

TEST(MulticlassClientProtocolTest, WhoClassMask_matchesWhoBitConvention)
{
    // WHO filter tests `classmask & (1 << classId)`; the active mask must use the same convention.
    EXPECT_EQ(WhoClassMask({1}), 1u << 1);                    // Warrior only
    EXPECT_EQ(WhoClassMask({1, 8, 11}), (1u << 1) | (1u << 8) | (1u << 11));
    EXPECT_EQ(WhoClassMask({}), 0u);
}
