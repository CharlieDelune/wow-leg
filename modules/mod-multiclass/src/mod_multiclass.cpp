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
          PLAYERHOOK_ON_GIVE_EXP,
          PLAYERHOOK_ON_GET_EFFECTIVE_CLASS_MASK,
          PLAYERHOOK_ON_GET_EFFECTIVE_CLASS_LEVEL,
          PLAYERHOOK_ON_GET_UNLOCKED_CLASS_MASK,
          PLAYERHOOK_ON_GET_UNLOCKED_CLASS_LEVEL }) { }

    void OnPlayerLoadFromDB(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        // Load slot state BEFORE the core's _LoadSkills / _LoadSpells run (this hook fires earlier in
        // Player::LoadFromDB). That way GetEffectiveClassMask() already reflects the active off-classes
        // when the core validates skills/spells, so an active off-class's skill lines and spells survive
        // the render-class check instead of being deleted. OnPlayerLogin reloads to a clean final state.
        Multiclass::LoadState(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        Multiclass::LoadState(player);
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
        if (!Multiclass::FindState(player->GetGUID()))
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

    void OnPlayerGetEffectiveClassMask(Player const* player, uint32& classMask) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        Multiclass::PlayerState* state = Multiclass::FindState(player->GetGUID());
        if (!state)
            return;

        // Union the active-class bits into the render-class mask (never a narrowing).
        classMask |= Multiclass::ActiveClassMask(state->slots);
    }

    void OnPlayerGetEffectiveClassLevel(Player const* player, uint8 classId, uint8& level) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        Multiclass::PlayerState* state = Multiclass::FindState(player->GetGUID());
        if (!state)
            return;

        // Report this class's own slot level; leave the default (displayed level) if it is not
        // one of the player's active classes.
        if (uint8 classLevel = Multiclass::ClassLevel(state->slots, classId))
            level = classLevel;
    }

    void OnPlayerGetUnlockedClassMask(Player const* player, uint32& classMask) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;
        Multiclass::PlayerState* state = Multiclass::FindState(player->GetGUID());
        if (!state)
            return;
        classMask |= Multiclass::UnlockedClassMask(state->pool);  // never narrows the render class
    }

    void OnPlayerGetUnlockedClassLevel(Player const* player, uint8 classId, uint8& level) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;
        Multiclass::PlayerState* state = Multiclass::FindState(player->GetGUID());
        if (!state)
            return;
        // Active class: the authoritative live level is the slot (XP routing updates it mid-session,
        // the pool may be stale until the next save). Benched class: the remembered pool level.
        for (Multiclass::ClassProgress const& cp : state->slots)
            if (cp.classId == classId)
            {
                level = cp.level;
                return;
            }
        if (uint8 const poolLevel = Multiclass::UnlockedClassLevel(state->pool, classId))
            level = poolLevel;
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
            { "info",        HandleInfo,        SEC_PLAYER,     Console::No },
            { "setclass",    HandleSetClass,    SEC_GAMEMASTER, Console::No },
            { "setlevel",    HandleSetLevel,    SEC_GAMEMASTER, Console::No },
            { "unlock",      HandleUnlock,      SEC_GAMEMASTER, Console::No },
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
        Multiclass::ReconcileDisplayLevel(player);
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

        if (classId == 0 || classId >= MAX_CLASSES)
        {
            handler->SendErrorMessage("Usage: .multiclass unlockclass <classId 1-11>");
            return true;
        }

        Multiclass::UnlockClass(player, classId);
        handler->PSendSysMessage("Class {} unlocked (benched). Slot it to make it active.", classId);
        return true;
    }
};

void Addmod_multiclassScripts()
{
    new multiclass_playerscript();
    new multiclass_commandscript();
}
