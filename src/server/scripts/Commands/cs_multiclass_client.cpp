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

#include "Chat.h"
#include "MulticlassClientProtocol.h"
#include "MulticlassEngine.h"
#include "MulticlassProfile.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

// MCLS client-panel bridge: validates hello/setorder requests on the addon channel, drives the engine.
class multiclass_client_playerscript : public PlayerScript
{
public:
    multiclass_client_playerscript() :
        PlayerScript("multiclass_client_playerscript", { PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE })
    {
    }

    void OnPlayerBeforeSendChatMessage(Player* player, uint32& /*type*/, uint32& lang, std::string& msg) override
    {
        if (lang != LANG_ADDON || !player)
            return;
        std::string_view const kTag = Multiclass::kClientMsgTag;
        if (msg.size() < kTag.size() || std::string_view(msg).substr(0, kTag.size()) != kTag)
            return;
        std::string_view const payload = std::string_view(msg).substr(kTag.size());
        Multiclass::ClientRequest const req = Multiclass::ParseClientRequest(payload);

        bool const enabled = sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE);
        MulticlassProfile& mc = player->GetMulticlassProfile();

        switch (req.verb)
        {
            case Multiclass::ClientVerb::Hello:
                Multiclass::SendClientState(player);
                break;
            case Multiclass::ClientVerb::SetOrder:
            {
                Multiclass::DenyReason const deny =
                    Multiclass::ValidateSetOrderRequest(mc, req.order, player->IsInCombat(), enabled);
                if (deny != Multiclass::DenyReason::Ok)
                {
                    SendError(player, Multiclass::DenyReasonText(deny));
                    break;
                }
                Multiclass::SetActiveOrder(player, req.order);
                // FinalizeActiveSetChange pushes fresh state; none needed here.
                break;
            }
            case Multiclass::ClientVerb::Whois:
            {
                if (!enabled)
                    break;   // feature off: no peer replies (client stays byte-vanilla)
                for (std::string const& name : req.names)
                {
                    Player* target = ObjectAccessor::FindPlayerByName(name, false);
                    if (target)
                        Multiclass::SendPeer(player, name, target->GetMulticlassProfile().GetActiveClasses());
                    else
                        Multiclass::SendPeer(player, name, {});   // unknown/offline -> empty peer
                }
                break;
            }
            case Multiclass::ClientVerb::SpendTalent:
            {
                if (!enabled)
                    break;
                if (!mc.HasActiveClass(req.talentClass))
                {
                    SendError(player, "That class isn't active.");
                    break;
                }
                if (!player->SpendClassTalent(req.talentClass, req.talentId, req.talentRank))
                    SendError(player, "That talent can't be learned right now.");
                Multiclass::SendClientState(player);   // re-push either way so the UI re-syncs
                break;
            }
            case Multiclass::ClientVerb::ResetTalents:
            {
                if (!enabled)
                    break;
                if (!mc.HasActiveClass(req.talentClass))
                {
                    SendError(player, "That class isn't active.");
                    break;
                }
                player->ResetClassTalents(req.talentClass);   // free bulk reset (Loadouts Phase 0); may no-op
                Multiclass::SendClientState(player);
                break;
            }
            case Multiclass::ClientVerb::RemoveTalent:
            {
                if (!enabled)
                    break;
                if (!mc.HasActiveClass(req.talentClass))
                {
                    SendError(player, "That class isn't active.");
                    break;
                }
                player->RemoveClassTalent(req.talentClass, req.talentId);   // free per-point removal + cascade
                Multiclass::SendClientState(player);
                break;
            }
            case Multiclass::ClientVerb::SocketGlyph:
            {
                if (!enabled)
                    break;
                if (!mc.HasActiveClass(req.glyphClass))
                {
                    SendError(player, "That class isn't active.");
                    break;
                }
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(req.glyphItemId);
                if (!proto || proto->Class != ITEM_CLASS_GLYPH || uint8(proto->SubClass) != req.glyphClass)
                {
                    SendError(player, "That glyph isn't for this class.");
                    break;
                }
                if (player->GetItemCount(req.glyphItemId, false) == 0)
                {
                    SendError(player, "You don't have that glyph.");
                    break;
                }
                uint32 const glyphId = Multiclass::GlyphIdFromItem(proto);
                if (glyphId == 0 || !player->ApplyClassGlyph(req.glyphClass, req.glyphSlot, glyphId))
                {
                    SendError(player, "That glyph can't go in that slot.");
                    break;
                }
                player->DestroyItemCount(req.glyphItemId, 1, true);   // consume one; economy intact
                Multiclass::SendClientState(player);
                break;
            }
            case Multiclass::ClientVerb::RemoveGlyph:
            {
                if (!enabled)
                    break;
                if (!mc.HasActiveClass(req.glyphClass))
                {
                    SendError(player, "That class isn't active.");
                    break;
                }
                player->RemoveClassGlyph(req.glyphClass, req.glyphSlot);   // free
                Multiclass::SendClientState(player);
                break;
            }
            case Multiclass::ClientVerb::Invalid:
            default:
                break;
        }

        // Consume it so it isn't relayed as a real whisper.
        msg.clear();
    }

private:
    static void SendError(Player* player, char const* text)
    {
        if (!player->GetSession())
            return;
        std::string payload = std::string(Multiclass::kClientMsgTag) + "err ";
        payload += text;
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, payload);
        player->GetSession()->SendPacket(&data);
    }
};

void AddSC_multiclass_client()
{
    new multiclass_client_playerscript();
}
