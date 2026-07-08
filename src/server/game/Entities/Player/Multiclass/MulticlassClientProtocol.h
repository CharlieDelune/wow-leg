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

#ifndef ACORE_MULTICLASS_CLIENT_PROTOCOL_H
#define ACORE_MULTICLASS_CLIENT_PROTOCOL_H

#include "MulticlassProfile.h"
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Multiclass
{
    // On-wire body prefix shared by every MCLS addon-channel message: "MCLS\tpayload".
    inline constexpr std::string_view kClientMsgTag = "MCLS\t";

    enum class ClientVerb : uint8 { Invalid, Hello, SetOrder, Whois, SpendTalent, ResetTalents, SocketGlyph, RemoveGlyph };

    struct ClientRequest
    {
        ClientVerb verb = ClientVerb::Invalid;
        std::vector<uint8> order;          // SetOrder: desired active classIds in slot order (slot 0 first)
        std::vector<std::string> names;    // Whois: player names to resolve
        uint8 talentClass = 0;             // SpendTalent / ResetTalents: the target active class
        uint32 talentId = 0;               // SpendTalent: DBC TalentID
        uint8 talentRank = 0;              // SpendTalent: 0-based rank index to add (== current point count)
        uint8 glyphClass = 0;              // SocketGlyph / RemoveGlyph: the target active class (ring)
        uint8 glyphSlot = 0;               // SocketGlyph / RemoveGlyph: glyph slot 0..5
        uint32 glyphItemId = 0;            // SocketGlyph: item entry of the glyph being socketed
    };

    enum class DenyReason : uint8
    {
        Ok, FeatureOff, InCombat, InvalidClass, NotOwned, TooMany, Duplicate, NoneActive
    };

    inline bool ParseU8(std::string_view tok, uint8& out)
    {
        if (tok.empty())
            return false;
        uint32 value = 0;
        auto const result = std::from_chars(tok.data(), tok.data() + tok.size(), value);
        if (result.ec != std::errc() || result.ptr != tok.data() + tok.size() || value > 255)
            return false;
        out = static_cast<uint8>(value);
        return true;
    }

    inline bool ParseU32(std::string_view tok, uint32& out)
    {
        if (tok.empty())
            return false;
        uint32 value = 0;
        auto const result = std::from_chars(tok.data(), tok.data() + tok.size(), value);
        if (result.ec != std::errc() || result.ptr != tok.data() + tok.size())
            return false;
        out = value;
        return true;
    }

    // payload (prefix already stripped): "hello" | "setorder <classId> [<classId> ...]"
    inline ClientRequest ParseClientRequest(std::string_view payload)
    {
        ClientRequest req;
        std::vector<std::string_view> tok;
        std::size_t i = 0;
        while (i < payload.size())
        {
            while (i < payload.size() && payload[i] == ' ')
                ++i;
            std::size_t const start = i;
            while (i < payload.size() && payload[i] != ' ')
                ++i;
            if (i > start)
                tok.emplace_back(payload.substr(start, i - start));
        }
        if (tok.empty())
            return req;
        if (tok[0] == "hello" && tok.size() == 1)
            req.verb = ClientVerb::Hello;
        else if (tok[0] == "setorder" && tok.size() >= 2)
        {
            std::vector<uint8> order;
            order.reserve(tok.size() - 1);
            bool ok = true;
            for (std::size_t t = 1; t < tok.size() && ok; ++t)
            {
                uint8 id = 0;
                if (ParseU8(tok[t], id))
                    order.push_back(id);
                else
                    ok = false;
            }
            if (ok)
            {
                req.verb = ClientVerb::SetOrder;
                req.order = std::move(order);
            }
        }
        else if (tok[0] == "whois" && tok.size() >= 2)
        {
            req.verb = ClientVerb::Whois;
            req.names.reserve(tok.size() - 1);
            for (std::size_t t = 1; t < tok.size(); ++t)
                req.names.emplace_back(tok[t]);
        }
        else if (tok[0] == "spendtalent" && tok.size() == 4)
        {
            uint8 c = 0; uint32 tId = 0; uint8 r = 0;
            if (ParseU8(tok[1], c) && ParseU32(tok[2], tId) && ParseU8(tok[3], r))
            {
                req.verb = ClientVerb::SpendTalent;
                req.talentClass = c;
                req.talentId = tId;
                req.talentRank = r;
            }
        }
        else if (tok[0] == "resettalents" && tok.size() == 2)
        {
            uint8 c = 0;
            if (ParseU8(tok[1], c))
            {
                req.verb = ClientVerb::ResetTalents;
                req.talentClass = c;
            }
        }
        else if (tok[0] == "socketglyph" && tok.size() == 4)
        {
            uint8 c = 0; uint8 s = 0; uint32 item = 0;
            if (ParseU8(tok[1], c) && ParseU8(tok[2], s) && ParseU32(tok[3], item))
            {
                req.verb = ClientVerb::SocketGlyph;
                req.glyphClass = c;
                req.glyphSlot = s;
                req.glyphItemId = item;
            }
        }
        else if (tok[0] == "removeglyph" && tok.size() == 3)
        {
            uint8 c = 0; uint8 s = 0;
            if (ParseU8(tok[1], c) && ParseU8(tok[2], s))
            {
                req.verb = ClientVerb::RemoveGlyph;
                req.glyphClass = c;
                req.glyphSlot = s;
            }
        }
        return req;
    }

    // Validate a whole desired active set (slot order). The panel sends the full set every time, so this
    // subsumes activate / bench / promote / reorder: any of them is just a different `order`.
    inline DenyReason ValidateSetOrderRequest(MulticlassProfile const& mc, std::vector<uint8> const& order,
        bool inCombat, bool featureEnabled)
    {
        if (!featureEnabled)
            return DenyReason::FeatureOff;
        if (inCombat)
            return DenyReason::InCombat;
        if (order.empty())
            return DenyReason::NoneActive;                 // a managed character must keep >= 1 active class
        if (order.size() > mc.GetMaxActiveClasses())
            return DenyReason::TooMany;
        for (std::size_t a = 0; a < order.size(); ++a)
        {
            uint8 const classId = order[a];
            if (!MulticlassProfile::IsValidClassId(classId))
                return DenyReason::InvalidClass;
            if (!mc.HasOwnedClass(classId))
                return DenyReason::NotOwned;               // the panel only arranges OWNED classes
            for (std::size_t b = a + 1; b < order.size(); ++b)
                if (order[b] == classId)
                    return DenyReason::Duplicate;
        }
        return DenyReason::Ok;
    }

    inline std::string SerializeStateSnapshot(MulticlassProfile const& mc, bool featureEnabled)
    {
        std::string out = "state 1 ";
        out += featureEnabled ? "1 " : "0 ";
        out += std::to_string(mc.GetMaxActiveClasses());
        out += ' ';
        out += std::to_string(mc.GetProjectedClass());
        std::vector<uint8> const& slots = mc.GetSlots();
        for (uint8 const classId : mc.GetOwnedClasses())
        {
            int slot = -1;
            for (std::size_t s = 0; s < slots.size(); ++s)
                if (slots[s] == classId)
                {
                    slot = static_cast<int>(s);
                    break;
                }
            out += ' ';
            out += std::to_string(classId);
            out += ':';
            out += std::to_string(mc.GetClassLevel(classId));
            out += ':';
            out += std::to_string(slot);
        }
        return out;
    }

    // "diegetic <word>" — the realm's configured class-agnostic word (may contain spaces, so it is the rest
    // of the line). The client expands the {mcU}/{mcL} markers in cached narrative text with it for multiclass
    // viewers. An empty word means the diegetic feature is off (no markers are ever emitted).
    inline std::string SerializeDiegeticWord(std::string_view word)
    {
        std::string out = "diegetic ";
        out.append(word);
        return out;
    }

    // "peer <name> <activeClassId> ..." — active ids in slot order; empty list means unknown/offline.
    inline std::string SerializePeer(std::string_view name, std::vector<uint8> const& active)
    {
        std::string out = "peer ";
        out.append(name);
        for (uint8 const classId : active)
        {
            out += ' ';
            out += std::to_string(classId);
        }
        return out;
    }

    // "talents <classId> <freePoints> <talentId:rank> ..." — one message per active class; the rank value
    // is the point COUNT in that talent (unlearned talents are omitted). The client derives per-tab spent
    // + tier/prereq gating from these counts.
    inline std::string SerializeClassTalents(uint8 classId, uint32 freePoints,
        std::vector<std::pair<uint32, uint32>> const& ranks)
    {
        std::string out = "talents ";
        out += std::to_string(classId);
        out += ' ';
        out += std::to_string(freePoints);
        for (auto const& [talentId, rank] : ranks)
        {
            out += ' ';
            out += std::to_string(talentId);
            out += ':';
            out += std::to_string(rank);
        }
        return out;
    }

    // "glyphs <classId> <slot>:<glyphSpellId>:<enabled> ..." — one message per active class; slots 0..5 in
    // order, glyphSpellId 0 = empty, enabled 1 when that slot is unlocked for the class's own level. Slot
    // type (major/minor) is fixed by slot index and hardcoded client-side, so it is not on the wire.
    inline std::string SerializeClassGlyphs(uint8 classId, std::vector<std::pair<uint32, uint32>> const& slots)
    {
        std::string out = "glyphs ";
        out += std::to_string(classId);
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            out += ' ';
            out += std::to_string(i);
            out += ':';
            out += std::to_string(slots[i].first);
            out += ':';
            out += std::to_string(slots[i].second);
        }
        return out;
    }

    // Class mask over a player's active set using the /who bit convention (bit == classId, matching
    // MiscHandler's `classmask & (1 << classId)`), NOT the (classId - 1) convention used elsewhere.
    inline uint32 WhoClassMask(std::vector<uint8> const& active)
    {
        uint32 mask = 0;
        for (uint8 const classId : active)
            mask |= (1u << classId);
        return mask;
    }

    inline char const* DenyReasonText(DenyReason reason)
    {
        switch (reason)
        {
            case DenyReason::FeatureOff:   return "Multiclass is disabled.";
            case DenyReason::InCombat:     return "You can't change classes in combat.";
            case DenyReason::InvalidClass: return "That is not a valid class.";
            case DenyReason::NotOwned:     return "You haven't unlocked that class.";
            case DenyReason::TooMany:      return "That's more classes than you can keep active.";
            case DenyReason::Duplicate:    return "A class can't be active twice.";
            case DenyReason::NoneActive:   return "You must keep at least one active class.";
            case DenyReason::Ok:           return "";
        }
        return "";
    }
}

#endif
