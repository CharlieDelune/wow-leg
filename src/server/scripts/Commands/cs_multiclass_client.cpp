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
