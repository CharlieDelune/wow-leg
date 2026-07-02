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
        { PLAYERHOOK_ON_LOAD_FROM_DB, PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_SAVE,
          PLAYERHOOK_ON_LEARN_SPELL, PLAYERHOOK_ON_FORGOT_SPELL,
          PLAYERHOOK_ON_GIVE_EXP }) { }

    void OnPlayerLoadFromDB(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        // The class SET is loaded by core (Player::_LoadMulticlassProfile) just before this hook fires,
        // so getClassMask() already reflects the active off-classes when the core validates
        // skills/spells (their skill lines and spells survive). Here we load only the per-class spell
        // ledger for the active classes. OnPlayerLogin reloads to a clean final state.
        Multiclass::LoadLedger(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        Multiclass::LoadLedger(player);
        Multiclass::BackfillActiveLedgers(player);
        Multiclass::ReconcileDisplayLevel(player);
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

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* victim, uint8 xpSource) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;
        if (amount == 0)
            return;
        // Only take over XP for a player we actually manage; never zero an unmanaged
        // player's XP.
        if (!Multiclass::FindLedger(player->GetGUID()))
            return;

        // This hook fires at the XP source, BEFORE native GiveXP's own eligibility guards
        // (Player.cpp). Replicate the two reachable ones and, on either, return WITHOUT
        // zeroing amount: native GiveXP then re-applies the same guard and also grants
        // nothing, so classes and character stay in exact lockstep (no XP either place).
        // Mirror native's death guard exactly (Player.cpp): dead players get no XP EXCEPT
        // LFG-dungeon completion rewards (native's isLFGReward == xpSource XPSOURCE_QUEST_DF),
        // which we still route to the classes rather than let native grant to the render char.
        if (!player->IsAlive() && !player->GetBattlegroundId() && xpSource != PlayerXPSource::XPSOURCE_QUEST_DF)
            return;
        if (player->HasPlayerFlag(PLAYER_FLAGS_NO_XP_GAIN))
            return;

        uint32 const base = amount;
        // Mirror native: rested applies only on kills (victim != nullptr). GetXPRestBonus
        // both computes the bonus AND drains the rested pool, exactly once per award.
        uint32 const bonus = victim ? player->GetXPRestBonus(base) : 0;
        Multiclass::RouteExperience(player, base + bonus);

        amount = 0;   // suppress the native single-class XP path; the module owns all XP
    }

    void OnPlayerSave(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        Multiclass::SaveLedger(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        Multiclass::UnloadLedger(player);
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
            { "info",        HandleInfo,        SEC_PLAYER,     Console::No },
            { "setclass",    HandleSetClass,    SEC_GAMEMASTER, Console::No },
            { "setlevel",    HandleSetLevel,    SEC_GAMEMASTER, Console::No },
            { "unlockclass", HandleUnlockClass, SEC_GAMEMASTER, Console::No }
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

        MulticlassProfile const& mc = player->GetMulticlassProfile();
        std::vector<uint8> const active = mc.GetActiveClasses();
        handler->PSendSysMessage("Multiclass (projected class {}, display level {}, max active {}):",
            mc.GetProjectedClass(), player->GetLevel(), mc.GetMaxActiveClasses());

        for (std::size_t slot = 0; slot < active.size(); ++slot)
        {
            uint8 const classId = active[slot];
            handler->PSendSysMessage("  slot {}: class {} level {} xp {}",
                uint32(slot), classId, mc.GetClassLevel(classId), mc.GetClassXp(classId));
        }

        handler->PSendSysMessage("Owned pool:");
        for (uint8 classId : mc.GetOwnedClasses())
            handler->PSendSysMessage("  class {} level {} xp {} {}",
                classId, mc.GetClassLevel(classId), mc.GetClassXp(classId),
                mc.HasActiveClass(classId) ? "(active)" : "(benched)");

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

        MulticlassProfile& mc = player->GetMulticlassProfile();
        std::vector<uint8> const active = mc.GetActiveClasses();

        if (!MulticlassProfile::IsValidClassId(classId))
        {
            handler->SendErrorMessage("Invalid class id (1-11, excluding 10).");
            return true;
        }
        // Slot may target an existing active slot (replace) or the next free one (append), within the cap.
        if (slot >= mc.GetMaxActiveClasses() || std::size_t(slot) > active.size())
        {
            handler->SendErrorMessage("Invalid slot: use 0..{} (next free slot is {}).",
                mc.GetMaxActiveClasses() - 1, active.size());
            return true;
        }
        // Reject a class already active in a DIFFERENT slot (a no-op self-set on this slot is fine).
        for (std::size_t s = 0; s < active.size(); ++s)
            if (active[s] == classId && s != std::size_t(slot))
            {
                handler->SendErrorMessage("Class {} is already active in slot {}.", classId, uint32(s));
                return true;
            }

        Multiclass::SwapSlotClass(player, slot, classId);
        handler->PSendSysMessage("Slot {} set to class {} (level {}).", slot, classId, mc.GetClassLevel(classId));
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

        if (level < 1 || level > 80)
        {
            handler->SendErrorMessage("Usage: .multiclass setlevel <slot> <level 1-80>");
            return true;
        }

        MulticlassProfile& mc = player->GetMulticlassProfile();
        std::vector<uint8> const active = mc.GetActiveClasses();
        if (std::size_t(slot) >= active.size())
        {
            handler->SendErrorMessage("Slot {} is empty.", slot);
            return true;
        }

        uint8 const classId = active[slot];
        mc.SetClassProgress(classId, level, mc.GetClassXp(classId));
        player->SaveMulticlassProfile();
        Multiclass::ReconcileDisplayLevel(player);
        handler->PSendSysMessage("Slot {} (class {}) level set to {}.", slot, classId, level);
        return true;
    }

    static bool HandleUnlockClass(ChatHandler* handler, uint8 classId)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
        {
            handler->SendErrorMessage("The multiclass module is disabled.");
            return true;
        }

        if (!MulticlassProfile::IsValidClassId(classId))
        {
            handler->SendErrorMessage("Usage: .multiclass unlockclass <classId 1-11, excluding 10>");
            return true;
        }

        Multiclass::UnlockClass(player, classId);
        player->SaveMulticlassProfile();   // persist the newly-owned class immediately (crash-safe)
        handler->PSendSysMessage("Class {} unlocked (benched). Slot it to make it active.", classId);
        return true;
    }
};

void Addmod_multiclassScripts()
{
    new multiclass_playerscript();
    new multiclass_commandscript();
}
