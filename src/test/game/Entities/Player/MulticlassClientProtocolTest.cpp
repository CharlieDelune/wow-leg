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

TEST(MulticlassClientProtocolTest, SerializeDiegeticWord)
{
    EXPECT_EQ(SerializeDiegeticWord("adventurer"), "diegetic adventurer");
    EXPECT_EQ(SerializeDiegeticWord("wandering hero"), "diegetic wandering hero");  // may contain spaces
    EXPECT_EQ(SerializeDiegeticWord(""), "diegetic ");                              // empty = feature off
}

TEST(MulticlassClientProtocolTest, WhoClassMask_matchesWhoBitConvention)
{
    // WHO filter tests `classmask & (1 << classId)`; the active mask must use the same convention.
    EXPECT_EQ(WhoClassMask({1}), 1u << 1);                    // Warrior only
    EXPECT_EQ(WhoClassMask({1, 8, 11}), (1u << 1) | (1u << 8) | (1u << 11));
    EXPECT_EQ(WhoClassMask({}), 0u);
}

TEST(MulticlassClientProtocolTest, ParseLoadoutNumericVerbs)
{
    EXPECT_EQ(ParseClientRequest("switchloadout 3").verb, ClientVerb::SwitchLoadout);
    EXPECT_EQ(ParseClientRequest("switchloadout 3").loadoutId, 3u);
    EXPECT_EQ(ParseClientRequest("delloadout 7").verb, ClientVerb::DelLoadout);
    EXPECT_EQ(ParseClientRequest("delloadout 7").loadoutId, 7u);
    EXPECT_EQ(ParseClientRequest("buyloadoutslot").verb, ClientVerb::BuyLoadoutSlot);
}

TEST(MulticlassClientProtocolTest, ParseLoadoutNamesRestOfLine)
{
    ClientRequest n = ParseClientRequest("newloadout Raid Fire");
    EXPECT_EQ(n.verb, ClientVerb::NewLoadout);
    EXPECT_EQ(n.loadoutText, "Raid Fire");

    ClientRequest d = ParseClientRequest("duploadout 5 Frost Clone");
    EXPECT_EQ(d.verb, ClientVerb::DupLoadout);
    EXPECT_EQ(d.loadoutId, 5u);
    EXPECT_EQ(d.loadoutText, "Frost Clone");

    ClientRequest r = ParseClientRequest("renameloadout 2 New Name");
    EXPECT_EQ(r.verb, ClientVerb::RenameLoadout);
    EXPECT_EQ(r.loadoutId, 2u);
    EXPECT_EQ(r.loadoutText, "New Name");
}

TEST(MulticlassClientProtocolTest, ParseLoadoutDescAndIcon)
{
    ClientRequest d = ParseClientRequest("descloadout 2 single target, no aoe");
    EXPECT_EQ(d.verb, ClientVerb::DescLoadout);
    EXPECT_EQ(d.loadoutId, 2u);
    EXPECT_EQ(d.loadoutText, "single target, no aoe");

    ClientRequest empty = ParseClientRequest("descloadout 2");   // clearing the description is allowed
    EXPECT_EQ(empty.verb, ClientVerb::DescLoadout);
    EXPECT_EQ(empty.loadoutText, "");

    ClientRequest i = ParseClientRequest("iconloadout 2 Interface\\Icons\\Spell_Fire_Fireball");
    EXPECT_EQ(i.verb, ClientVerb::IconLoadout);
    EXPECT_EQ(i.loadoutId, 2u);
    EXPECT_EQ(i.loadoutText, "Interface\\Icons\\Spell_Fire_Fireball");
}

TEST(MulticlassClientProtocolTest, ParseLoadoutRejectsBadInput)
{
    EXPECT_EQ(ParseClientRequest("newloadout").verb, ClientVerb::Invalid);        // empty name
    EXPECT_EQ(ParseClientRequest("duploadout 5").verb, ClientVerb::Invalid);      // id but no name
    EXPECT_EQ(ParseClientRequest("switchloadout").verb, ClientVerb::Invalid);     // missing id
    EXPECT_EQ(ParseClientRequest("switchloadout x").verb, ClientVerb::Invalid);   // non-numeric id
}

TEST(MulticlassClientProtocolTest, ParseOrderLoadouts)
{
    ClientRequest r = ParseClientRequest("orderloadouts 3 1 2");
    EXPECT_EQ(r.verb, ClientVerb::OrderLoadouts);
    ASSERT_EQ(r.loadoutOrder.size(), 3u);
    EXPECT_EQ(r.loadoutOrder[0], 3u);
    EXPECT_EQ(r.loadoutOrder[1], 1u);
    EXPECT_EQ(r.loadoutOrder[2], 2u);
    EXPECT_EQ(ParseClientRequest("orderloadouts 5").loadoutOrder.size(), 1u);   // single id is valid
    EXPECT_EQ(ParseClientRequest("orderloadouts").verb, ClientVerb::Invalid);   // needs >= 1 id
    EXPECT_EQ(ParseClientRequest("orderloadouts 1 x 2").verb, ClientVerb::Invalid);   // non-numeric id
}

TEST(MulticlassClientProtocolTest, SerializeLoadoutClasses)
{
    EXPECT_EQ(SerializeLoadoutClasses(3, {1, 8, 11}), "loadoutclasses 3 1 8 11");
    EXPECT_EQ(SerializeLoadoutClasses(4, {}), "loadoutclasses 4");   // empty set = just the id
}

TEST(MulticlassClientProtocolTest, SerializeLoadoutLines)
{
    EXPECT_EQ(SerializeLoadout(LoadoutMeta{3, "Raid Fire", "single target", "IconX", 0}, true),
        "loadout 3 0 1 IconX Raid Fire");
    EXPECT_EQ(SerializeLoadout(LoadoutMeta{4, "Blank", "", "", 2}, false),
        "loadout 4 2 0 - Blank");                                  // empty icon becomes "-"
    EXPECT_EQ(SerializeLoadoutDescription(LoadoutMeta{3, "Raid Fire", "single target", "IconX", 0}),
        "loadoutdesc 3 single target");
    EXPECT_EQ(SerializeLoadoutDescription(LoadoutMeta{4, "Blank", "", "", 2}),
        "loadoutdesc 4 ");                                         // empty description keeps the trailing space
    EXPECT_EQ(SerializeLoadoutCapacity(4, 2, 4000000), "loadoutcap 4 2 4000000");
}

TEST(MulticlassClientProtocolTest, ParseSetBarPrefs)
{
    ClientRequest r = ParseClientRequest("setbarprefs 1|6|1.00|CENTER|-40|120");
    EXPECT_EQ(r.verb, ClientVerb::SetBarPrefs);
    EXPECT_EQ(r.loadoutText, "1|6|1.00|CENTER|-40|120");

    ClientRequest empty = ParseClientRequest("setbarprefs");   // no blob = clear the saved settings
    EXPECT_EQ(empty.verb, ClientVerb::SetBarPrefs);
    EXPECT_EQ(empty.loadoutText, "");
}

TEST(MulticlassClientProtocolTest, SerializeBarPrefs)
{
    EXPECT_EQ(SerializeBarPrefs("1|6|1.00|CENTER|-40|120"), "barprefs 1|6|1.00|CENTER|-40|120");
    EXPECT_EQ(SerializeBarPrefs(""), "barprefs ");   // empty state still round-trips
}
