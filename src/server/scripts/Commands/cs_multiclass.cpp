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
#include "MulticlassEngine.h"
#include "Player.h"
#include "World.h"

using namespace Acore::ChatCommands;

class multiclass_commandscript : public CommandScript
{
public:
    multiclass_commandscript() : CommandScript("multiclass_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable mcTable =
        {
            { "info",        HandleInfo,        SEC_PLAYER,     Console::No },
            { "setslot",     HandleSetSlot,     SEC_GAMEMASTER, Console::No },
            { "unsetslot",   HandleUnsetSlot,   SEC_GAMEMASTER, Console::No },
            { "setlevel",    HandleSetLevel,    SEC_GAMEMASTER, Console::No },
            { "unlockclass", HandleUnlockClass, SEC_GAMEMASTER, Console::No },
            { "setcapacity", HandleSetCapacity, SEC_GAMEMASTER, Console::No }
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

        if (!sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE))
        {
            handler->SendErrorMessage("Multiclass is disabled.");
            return true;
        }

        MulticlassProfile const& mc = player->GetMulticlassProfile();
        handler->PSendSysMessage("Multiclass (projected class {}, display level {}, max active {}):",
            mc.GetProjectedClass(), player->GetLevel(), mc.GetMaxActiveClasses());
        handler->PSendSysMessage("  earned slots {}, server ceiling {}, effective cap {}",
            uint32(mc.GetUnlockedSlots()), uint32(mc.GetActiveCeiling()), uint32(mc.GetMaxActiveClasses()));

        // Positional slots: show every position up to the cap so empty (unset) slots are visible as holes.
        for (uint8 slot = 0; slot < mc.GetMaxActiveClasses(); ++slot)
        {
            uint8 const classId = mc.GetClassAtSlot(slot);
            if (classId == 0)
                handler->PSendSysMessage("  slot {}: (empty)", uint32(slot));
            else
                handler->PSendSysMessage("  slot {}: class {} level {} xp {}",
                    uint32(slot), uint32(classId), mc.GetClassLevel(classId), mc.GetClassXp(classId));
        }

        handler->PSendSysMessage("Owned pool:");
        for (uint8 classId : mc.GetOwnedClasses())
            handler->PSendSysMessage("  class {} level {} xp {} {}",
                classId, mc.GetClassLevel(classId), mc.GetClassXp(classId),
                mc.HasActiveClass(classId) ? "(active)" : "(benched)");

        return true;
    }

    static bool HandleSetSlot(ChatHandler* handler, uint8 slot, uint8 classId)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE))
        {
            handler->SendErrorMessage("Multiclass is disabled.");
            return true;
        }

        MulticlassProfile& mc = player->GetMulticlassProfile();

        if (!MulticlassProfile::IsValidClassId(classId))
        {
            handler->SendErrorMessage("Invalid class id (1-11, excluding 10).");
            return true;
        }
        // Any position within the cap is settable — fill an empty slot or replace an occupied one. Positions
        // are stable, so setting slot N never disturbs the others.
        if (slot >= mc.GetMaxActiveClasses())
        {
            handler->SendErrorMessage("Invalid slot: use 0..{}.", mc.GetMaxActiveClasses() - 1);
            return true;
        }
        // Reject a class already active in a DIFFERENT slot (a no-op self-set on this slot is fine).
        std::vector<uint8> const& slots = mc.GetSlots();
        for (std::size_t s = 0; s < slots.size(); ++s)
            if (slots[s] == classId && s != std::size_t(slot))
            {
                handler->SendErrorMessage("Class {} is already active in slot {}.", uint32(classId), uint32(s));
                return true;
            }

        Multiclass::SwapSlotClass(player, slot, classId);
        handler->PSendSysMessage("Slot {} set to class {} (level {}).",
            uint32(slot), uint32(classId), mc.GetClassLevel(classId));
        return true;
    }

    static bool HandleUnsetSlot(ChatHandler* handler, uint8 slot)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE))
        {
            handler->SendErrorMessage("Multiclass is disabled.");
            return true;
        }

        MulticlassProfile& mc = player->GetMulticlassProfile();
        uint8 const classId = mc.GetClassAtSlot(slot);
        if (classId == 0)
        {
            handler->SendErrorMessage("Slot {} is empty.", uint32(slot));
            return true;
        }
        // The last filled slot cannot be cleared: the projected class reads the slot-order front, so a
        // character must keep at least one active class. Swap it for a different class with setslot instead.
        if (mc.GetActiveClasses().size() <= 1)
        {
            handler->SendErrorMessage("Cannot unset the last active class; a character must keep at least one.");
            return true;
        }

        Multiclass::UnsetSlot(player, slot);
        handler->PSendSysMessage("Slot {} unset; class {} is now benched (still unlocked, re-slot it any time).",
            uint32(slot), uint32(classId));
        return true;
    }

    static bool HandleSetLevel(ChatHandler* handler, uint8 slot, uint8 level)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE))
        {
            handler->SendErrorMessage("Multiclass is disabled.");
            return true;
        }

        if (level < 1 || level > 80)
        {
            handler->SendErrorMessage("Usage: .multiclass setlevel <slot> <level 1-80>");
            return true;
        }

        MulticlassProfile& mc = player->GetMulticlassProfile();
        uint8 const classId = mc.GetClassAtSlot(slot);
        if (classId == 0)
        {
            handler->SendErrorMessage("Slot {} is empty.", uint32(slot));
            return true;
        }

        mc.SetClassProgress(classId, level, mc.GetClassXp(classId));
        Multiclass::GrantSlotCapacity(player, MulticlassProfile::SlotCapacityForLevel(mc.GetMaxOwnedLevel()));
        player->SaveMulticlassProfile();
        Multiclass::ReconcileDisplayLevel(player);
        handler->PSendSysMessage("Slot {} (class {}) level set to {}.", uint32(slot), uint32(classId), uint32(level));
        return true;
    }

    static bool HandleUnlockClass(ChatHandler* handler, uint8 classId)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE))
        {
            handler->SendErrorMessage("Multiclass is disabled.");
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

    static bool HandleSetCapacity(ChatHandler* handler, uint8 n)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE))
        {
            handler->SendErrorMessage("Multiclass is disabled.");
            return true;
        }

        if (n < 1)
        {
            handler->SendErrorMessage("Usage: .multiclass setcapacity <n> (n >= 1)");
            return true;
        }

        Multiclass::SetSlotCapacity(player, n);
        MulticlassProfile const& mc = player->GetMulticlassProfile();
        handler->PSendSysMessage("Earned slots set to {} (effective cap {} after the server ceiling).",
            uint32(mc.GetUnlockedSlots()), uint32(mc.GetMaxActiveClasses()));
        return true;
    }
};

void AddSC_multiclass_commandscript()
{
    new multiclass_commandscript();
}
