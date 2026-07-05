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
#include "DBCStores.h"
#include "MulticlassEngine.h"
#include "MulticlassLogic.h"
#include "Player.h"
#include "SpellDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
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
            { "setcapacity", HandleSetCapacity, SEC_GAMEMASTER, Console::No },
            { "talents",      HandleTalents,      SEC_GAMEMASTER, Console::No },
            { "spellcheck",   HandleSpellCheck,   SEC_GAMEMASTER, Console::No },
            { "resettalents", HandleResetTalents, SEC_GAMEMASTER, Console::No },
            { "glyphs",       HandleGlyphs,       SEC_GAMEMASTER, Console::No }
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

    // Server-side ground truth for the per-class talent MODEL, independent of the stock client's display:
    // per owned class the pool (Multiclass::TalentPointsForLevel), the spent count (SpentTalentPointsForClass),
    // and free = pool - spent, plus active/benched. The projected class's numbers should match the client's
    // single-class free-point register (shown in the header) -- a quick view/model consistency check.
    static bool HandleTalents(ChatHandler* handler)
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
        handler->PSendSysMessage("Talent state: display class {}, projected class {}, client free points {}.",
            uint32(player->getClass()), uint32(mc.GetProjectedClass()), player->GetFreeTalentPoints());

        for (uint8 classId : mc.GetOwnedClasses())
        {
            uint8 const level = mc.GetClassLevel(classId);
            uint32 const pool = Multiclass::TalentPointsForLevel(level);
            uint32 const spent = player->SpentTalentPointsForClass(classId);
            uint32 const freePts = pool > spent ? pool - spent : 0u;
            handler->PSendSysMessage("  class {} L{} {}: spent {}/{} pool (free {})",
                uint32(classId), uint32(level), mc.HasActiveClass(classId) ? "ACTIVE" : "BENCHED",
                spent, pool, freePts);
        }

        return true;
    }

    // The cast-time/cooldown/mana probe the stock client only renders as generic bars. Reports the server's
    // ENFORCED values for a spell -- base (raw SpellInfo) vs effective (after the player's live talent SpellMods
    // + haste), computed via the same functions the server runs on cast (ModSpellCastTime, ApplySpellMod,
    // CalcPowerCost) so a non-zero delta means the talent is functionally applied. Read-only: ApplySpellMod with
    // a null Spell* drops no mod charges (Player::ApplyModToSpell no-ops on null).
    static bool HandleSpellCheck(ChatHandler* handler, uint32 spellId)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
        {
            handler->SendErrorMessage("Unknown spell {}.", spellId);
            return true;
        }

        // Cast time: raw (level-scaled, no mods) vs effective (haste + talent SPELLMOD_CASTING_TIME), matching
        // Unit::ModSpellCastTime -- the value the server actually enforces on cast.
        int32 const castBase = int32(spellInfo->CalcCastTime(player));
        int32 castEff = castBase;
        player->ModSpellCastTime(spellInfo, castEff);

        // Global cooldown: base vs effective, mirroring Spell::TriggerGlobalCooldown (its MIN_GCD/MAX_GCD enum
        // is file-local to Spell.cpp: 1000/1500 ms).
        constexpr int32 kMinGcd = 1000;
        constexpr int32 kMaxGcd = 1500;
        int32 const gcdBase = int32(spellInfo->StartRecoveryTime);
        int32 gcdEff = gcdBase;
        if (gcdBase >= kMinGcd && gcdBase <= kMaxGcd)
        {
            player->ApplySpellMod(spellInfo->Id, SPELLMOD_GLOBAL_COOLDOWN, gcdEff);
            if (spellInfo->StartRecoveryCategory == 133 && spellInfo->StartRecoveryTime == 1500 &&
                spellInfo->DmgClass != SPELL_DAMAGE_CLASS_MELEE && spellInfo->DmgClass != SPELL_DAMAGE_CLASS_RANGED &&
                !spellInfo->HasAttribute(SPELL_ATTR0_USES_RANGED_SLOT) &&
                !spellInfo->HasAttribute(SPELL_ATTR0_IS_ABILITY))
                gcdEff = int32(float(gcdEff) * player->GetFloatValue(UNIT_MOD_CAST_SPEED));

            if (gcdEff < kMinGcd)
                gcdEff = kMinGcd;
            else if (gcdEff > kMaxGcd)
                gcdEff = kMaxGcd;
        }

        // Cooldown: base recovery vs talent SPELLMOD_COOLDOWN-adjusted.
        int32 const cdBase = int32(spellInfo->RecoveryTime);
        int32 cdEff = cdBase;
        player->ApplySpellMod(spellInfo->Id, SPELLMOD_COOLDOWN, cdEff);

        // Power/mana cost: raw ManaCost field vs effective CalcPowerCost, which folds in talent SPELLMOD_COST.
        int32 const costBase = int32(spellInfo->ManaCost);
        int32 const costEff = spellInfo->CalcPowerCost(player, spellInfo->GetSchoolMask());

        handler->PSendSysMessage("Spell {} effective values for {}:", spellId, player->GetName());
        handler->PSendSysMessage("  cast:     {} -> {} ms (delta {})", castBase, castEff, castEff - castBase);
        handler->PSendSysMessage("  gcd:      {} -> {} ms", gcdBase, gcdEff);
        handler->PSendSysMessage("  cooldown: {} -> {} ms (delta {})", cdBase, cdEff, cdEff - cdBase);
        handler->PSendSysMessage("  cost:     {} -> {} (delta {})", costBase, costEff, costEff - costBase);
        return true;
    }

    // Reset a specific owned class's talents (active or benched, in slot 0 or not) -- the client-independent
    // entry to the per-class reset primitive P4's per-class reset UI will call. Free and ladder-neutral
    // (noResetCost): this is a testing/admin lever, so it neither charges gold nor advances the class's
    // reset-cost ladder; exercise the escalating cost ladder through a class trainer instead.
    static bool HandleResetTalents(ChatHandler* handler, uint8 classId)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE))
        {
            handler->SendErrorMessage("Multiclass is disabled.");
            return true;
        }

        if (!player->GetMulticlassProfile().HasOwnedClass(classId))
        {
            handler->SendErrorMessage("You do not own class {}.", uint32(classId));
            return true;
        }

        if (player->ResetClassTalents(classId, /*noResetCost*/ true))
            handler->PSendSysMessage("Reset talents for class {} (free, ladder unchanged).", uint32(classId));
        else
            handler->PSendSysMessage("Class {} had no talents to reset.", uint32(classId));

        return true;
    }

    // Server-side ground truth for the per-class glyph MODEL. Per owned class: ACTIVE/BENCHED and its 6
    // slots -- socketed glyph id + its aura spell (run `.multiclass spellcheck <spell>` to prove the mod),
    // whether the slot is unlocked at the class's OWN level, and whether the aura is currently live on the
    // player. Client-independent proof that glyphs are attributed per class and active classes' auras apply.
    static bool HandleGlyphs(ChatHandler* handler)
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
        handler->PSendSysMessage("Glyph state: projected class {}.", uint32(mc.GetProjectedClass()));

        for (uint8 classId : mc.GetOwnedClasses())
        {
            uint8 const level = mc.GetClassLevel(classId);
            handler->PSendSysMessage("  class {} L{} {}:", uint32(classId), uint32(level),
                mc.HasActiveClass(classId) ? "ACTIVE" : "BENCHED");

            for (uint8 slot = 0; slot < MAX_GLYPH_SLOT_INDEX; ++slot)
            {
                uint8 const unlock = Multiclass::GlyphSlotUnlockLevel(slot);
                bool const open = unlock == 0 || level >= unlock;
                uint32 const glyph = player->GetClassGlyph(classId, slot);
                if (!glyph)
                {
                    handler->PSendSysMessage("    slot {} [{}]: empty", uint32(slot), open ? "open" : "locked");
                    continue;
                }

                GlyphPropertiesEntry const* glyphEntry = sGlyphPropertiesStore.LookupEntry(glyph);
                uint32 const spellId = glyphEntry ? glyphEntry->SpellId : 0u;
                bool const live = spellId != 0 && player->HasAura(spellId);
                handler->PSendSysMessage("    slot {} [{}]: glyph {} -> spell {} ({})",
                    uint32(slot), open ? "open" : "locked", glyph, spellId, live ? "aura LIVE" : "aura off");
            }
        }

        return true;
    }
};

void AddSC_multiclass_commandscript()
{
    new multiclass_commandscript();
}
