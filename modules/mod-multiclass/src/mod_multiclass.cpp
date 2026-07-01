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
#include "CommandScript.h"
#include "Config.h"
#include "Log.h"
#include "MulticlassState.h"
#include "Player.h"
#include "PlayerScript.h"

using namespace Acore::ChatCommands;

class multiclass_playerscript : public PlayerScript
{
public:
    multiclass_playerscript() : PlayerScript("multiclass_playerscript",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_SAVE,
          PLAYERHOOK_ON_LEARN_SPELL, PLAYERHOOK_ON_FORGOT_SPELL }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        Multiclass::LoadState(player);
        Multiclass::BackfillActiveLedgers(player);
        LOG_INFO("module.multiclass", "mod-multiclass loaded for player {}", player->GetGUID().ToString());
    }

    void OnPlayerLearnSpell(Player* player, uint32 spellID) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        Multiclass::AttributeLearnedSpell(player, spellID);
    }

    void OnPlayerForgotSpell(Player* player, uint32 spellID) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        Multiclass::AttributeForgotSpell(player, spellID);
    }

    void OnPlayerSave(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        Multiclass::SaveState(player->GetGUID());
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        Multiclass::UnloadState(player->GetGUID());
    }
};

class multiclass_commandscript : public CommandScript
{
public:
    multiclass_commandscript() : CommandScript("multiclass_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable mcTable =
        {
            { "info",     HandleInfo,     SEC_PLAYER,        Console::No },
            { "setclass", HandleSetClass, SEC_GAMEMASTER,    Console::No },
            { "setlevel", HandleSetLevel, SEC_GAMEMASTER,    Console::No },
            { "unlock",   HandleUnlock,   SEC_GAMEMASTER,    Console::No }
        };

        static ChatCommandTable commandTable =
        {
            { "multiclass", mcTable }
        };

        return commandTable;
    }

    static bool HandleInfo(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
        {
            handler->SendErrorMessage("The multiclass module is disabled.");
            return true;
        }

        Multiclass::PlayerState& state = Multiclass::GetOrCreateState(player);
        handler->PSendSysMessage("Multiclass (render class {}, display level {}):",
            state.renderClass, Multiclass::ComputeDisplayLevel(state.slots));

        for (uint8 slot = 0; slot < Multiclass::MAX_CLASS_SLOTS; ++slot)
        {
            Multiclass::ClassProgress const& cp = state.slots[slot];
            handler->PSendSysMessage("  slot {}: class {} level {} xp {} {}",
                slot, cp.classId, cp.level, cp.xp, state.unlocked[slot] ? "" : "(locked)");
        }

        return true;
    }

    static bool HandleSetClass(ChatHandler* handler, uint8 slot, uint8 classId)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
        {
            handler->SendErrorMessage("The multiclass module is disabled.");
            return true;
        }

        Multiclass::PlayerState& state = Multiclass::GetOrCreateState(player);
        if (!Multiclass::CanAssignClass(state.slots, slot, classId))
        {
            handler->SendErrorMessage("Invalid slot/class, or class already assigned to another slot.");
            return true;
        }

        Multiclass::SwapSlotClass(player, slot, classId);
        handler->PSendSysMessage("Slot {} set to class {} (level {}).", slot, classId, state.slots[slot].level);
        return true;
    }

    static bool HandleSetLevel(ChatHandler* handler, uint8 slot, uint8 level)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
        {
            handler->SendErrorMessage("The multiclass module is disabled.");
            return true;
        }

        if (slot >= Multiclass::MAX_CLASS_SLOTS || level < 1 || level > 80)
        {
            handler->SendErrorMessage("Usage: .multiclass setlevel <slot 0-2> <level 1-80>");
            return true;
        }

        Multiclass::PlayerState& state = Multiclass::GetOrCreateState(player);
        if (state.slots[slot].classId == 0)
        {
            handler->SendErrorMessage("Slot {} is empty.", slot);
            return true;
        }

        state.slots[slot].level = level;
        Multiclass::SaveState(player->GetGUID());
        handler->PSendSysMessage("Slot {} level set to {}.", slot, level);
        return true;
    }

    static bool HandleUnlock(ChatHandler* handler, uint8 slot)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
        {
            handler->SendErrorMessage("The multiclass module is disabled.");
            return true;
        }

        if (slot >= Multiclass::MAX_CLASS_SLOTS)
        {
            handler->SendErrorMessage("Usage: .multiclass unlock <slot 0-2>");
            return true;
        }

        Multiclass::PlayerState& state = Multiclass::GetOrCreateState(player);
        state.unlocked[slot] = true;
        Multiclass::SaveState(player->GetGUID());
        handler->PSendSysMessage("Slot {} unlocked.", slot);
        return true;
    }
};

void Addmod_multiclassScripts()
{
    new multiclass_playerscript();
    new multiclass_commandscript();
}
