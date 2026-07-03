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

        if (!sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE))
        {
            handler->SendErrorMessage("Multiclass is disabled.");
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

        if (!sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE))
        {
            handler->SendErrorMessage("Multiclass is disabled.");
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
};

void AddSC_multiclass_commandscript()
{
    new multiclass_commandscript();
}
