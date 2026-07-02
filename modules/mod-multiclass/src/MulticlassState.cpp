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

#include "MulticlassState.h"
#include "MulticlassSpells.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <unordered_map>

namespace
{
    std::unordered_map<ObjectGuid, Multiclass::Ledger> g_ledgers;
    bool g_inOrchestration = false;  // suppress learn/forget capture during our own grants/removes

    // A spell is class-attributable (bankable/ledgerable) only if it is a genuine class ability. Primary
    // and secondary professions carry per-class SkillRaceClassInfo entries, so the ownership resolver would
    // otherwise treat First Aid / Cooking / Alchemy / etc. as class-owned and bank them per class. Professions
    // are class-agnostic -- exclude any spell whose skill line is a profession or secondary-profession category.
    bool IsClassAttributableSpell(uint32 spellId)
    {
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineEntry const* line = sSkillLineStore.LookupEntry(itr->second->SkillLine);
            if (line && (line->categoryId == SKILL_CATEGORY_PROFESSION || line->categoryId == SKILL_CATEGORY_SECONDARY))
                return false;
        }
        return true;
    }
}

namespace Multiclass
{
    Ledger* FindLedger(ObjectGuid guid)
    {
        auto itr = g_ledgers.find(guid);
        return itr == g_ledgers.end() ? nullptr : &itr->second;
    }

    // The class SET is loaded by core (Player::_LoadMulticlassProfile) before this runs; here we load
    // only the per-class learned-spell ledger, for the classes currently active in the profile.
    void LoadLedger(Player* player)
    {
        ObjectGuid const guid = player->GetGUID();
        uint32 const low = guid.GetCounter();
        Ledger& ledger = g_ledgers[guid];
        ledger.clear();

        for (uint8 classId : player->GetMulticlassProfile().GetActiveClasses())
        {
            if (QueryResult result = CharacterDatabase.Query(Acore::StringFormat(
                "SELECT `spellId` FROM `character_multiclass_spell` WHERE `guid` = {} AND `classId` = {}",
                low, classId)))
            {
                do
                {
                    ledger[classId].insert(result->Fetch()[0].Get<uint32>());
                } while (result->NextRow());
            }
        }
    }

    Ledger& GetOrCreateLedger(Player* player)
    {
        if (Ledger* existing = FindLedger(player->GetGUID()))
            return *existing;
        LoadLedger(player);
        return g_ledgers[player->GetGUID()];
    }

    // Persist only `character_multiclass_spell`, for the active classes. Core owns the set tables
    // (character_multiclass_class / _slot) via Player::_SaveMulticlassProfile.
    void SaveLedger(Player* player, bool sync)
    {
        Ledger* ledger = FindLedger(player->GetGUID());
        if (!ledger)
            return;

        uint32 const low = player->GetGUID().GetCounter();
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        for (uint8 classId : player->GetMulticlassProfile().GetActiveClasses())
        {
            trans->Append(Acore::StringFormat(
                "DELETE FROM `character_multiclass_spell` WHERE `guid` = {} AND `classId` = {}", low, classId));

            auto itr = ledger->find(classId);
            if (itr != ledger->end())
                for (uint32 spellId : itr->second)
                    trans->Append(Acore::StringFormat(
                        "INSERT INTO `character_multiclass_spell` (`guid`, `classId`, `spellId`) VALUES ({}, {}, {})",
                        low, classId, spellId));
        }
        // Sync commit when a later step in the SAME world tick reads these rows back: SwapSlotClass banks
        // the outgoing class right before a possible re-activation, and ActivateClass's remembered-branch
        // restore is a synchronous pool query with no happens-before against an async commit -- an async
        // bank could be read back empty. Async everywhere else (periodic save, logout).
        if (sync)
            CharacterDatabase.DirectCommitTransaction(trans);
        else
            CharacterDatabase.CommitTransaction(trans);
    }

    void UnloadLedger(Player* player)
    {
        SaveLedger(player);
        g_ledgers.erase(player->GetGUID());
    }

    void UnlockClass(Player* player, uint8 classId)
    {
        if (!MulticlassProfile::IsValidClassId(classId))
            return;

        MulticlassProfile& mc = player->GetMulticlassProfile();
        if (mc.HasOwnedClass(classId))
            return;  // already unlocked -> idempotent

        // The profile owns the pool; core persists the `_class` row on save (callers that unlock
        // outside a swap also call Player::SaveMulticlassProfile to keep the unlock crash-safe).
        mc.AddOwnedClass(classId, 1, 0);

        // Bank the creation kit into the class's `_spell` book (benched -- it goes live only via
        // ActivateClass). Committed as one transaction so ActivateClass's restore never reads a partial kit.
        uint32 const low = player->GetGUID().GetCounter();
        auto const starters = StartingSpellsFor(player, classId);
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        for (uint32 spellId : starters)
            trans->Append(Acore::StringFormat(
                "INSERT INTO `character_multiclass_spell` (`guid`, `classId`, `spellId`) VALUES ({}, {}, {})"
                " ON DUPLICATE KEY UPDATE `spellId` = `spellId`", low, classId, spellId));
        // Synchronous: a follow-up ActivateClass (same tick, e.g. `unlockclass` then `setclass`) reads
        // this kit back with a sync query and must see it committed, not an in-flight async write.
        CharacterDatabase.DirectCommitTransaction(trans);

        LOG_INFO("module.multiclass", "UnlockClass: guid {} unlocked class {} with {} starter spells",
            low, classId, starters.size());
    }

    void ActivateClass(Player* player, uint8 slot, uint8 classId)
    {
        MulticlassProfile& mc = player->GetMulticlassProfile();
        Ledger& ledger = GetOrCreateLedger(player);
        uint32 const low = player->GetGUID().GetCounter();

        std::unordered_set<uint32> spells;

        // Owned in the profile == remembered (unlocked/slotted before): restore the banked spellbook from
        // `_spell`; the class's level/xp already live in the profile pool. This is an in-memory membership
        // check, so -- unlike the old committed-row probe -- it cannot race an async write.
        if (mc.HasOwnedClass(classId))
        {
            if (QueryResult spellRows = CharacterDatabase.Query(Acore::StringFormat(
                "SELECT `spellId` FROM `character_multiclass_spell` WHERE `guid` = {} AND `classId` = {}",
                low, classId)))
            {
                do
                {
                    spells.insert(spellRows->Fetch()[0].Get<uint32>());
                } while (spellRows->NextRow());
            }
        }
        else
        {
            // First time this class is assigned: unlock it (adds it to the profile pool at level 1 and
            // banks the kit into `_spell`) then start fresh with the creation loadout, learned LIVE from
            // StartingSpellsFor in-memory.
            UnlockClass(player, classId);
            for (uint32 spellId : StartingSpellsFor(player, classId))
                spells.insert(spellId);
            if (spells.empty())
                LOG_INFO("module.multiclass",
                    "ActivateClass: no creation spells for race {} class {}"
                    " (race-invalid combo); class starts spell-less",
                    player->getRace(), classId);
        }

        // Place the class at the requested active position (owned by now, in both branches).
        bool const placed = std::size_t(slot) < mc.GetActiveClasses().size()
            ? mc.ReplaceActiveAt(slot, classId)
            : mc.Activate(classId);
        if (!placed)
        {
            // Defensive: the command path pre-validates slot/cap/duplicates, so this should not fire.
            // Bail before granting so an unvalidated future caller can never leave a class
            // learned-but-not-active (skills/spells granted while the active set stayed unchanged).
            LOG_ERROR("module.multiclass", "ActivateClass: could not place class {} at slot {} for guid {}",
                classId, slot, low);
            return;
        }

        g_inOrchestration = true;
        // Grant the class's skill lines (idempotent) so the client renders its abilities and trainers; runs
        // under the guard because SetSkill auto-learns each line's general spells, which the learn hook
        // would otherwise mis-attribute.
        GrantClassSkills(player, classId);
        for (uint32 spellId : spells)
            player->learnSpell(spellId, false);
        g_inOrchestration = false;

        ledger[classId] = std::move(spells);
    }

    void AttributeLearnedSpell(Player* player, uint32 spellId)
    {
        if (g_inOrchestration)
            return;

        Ledger* ledger = FindLedger(player->GetGUID());
        if (!ledger)
            return;

        // Talent spells are tracked by the core talent map, not the ledger.
        if (IsTalentSpell(spellId))
            return;

        if (!IsClassAttributableSpell(spellId))
            return;  // professions are class-agnostic: never bank, ledger, or remove them

        bool skillLineDummy = false;
        uint32 const activeOwnerMask = player->GetSpellClassOwners(
            spellId, player->getClassMask(), skillLineDummy);
        uint32 const unlockedOwnerMask = player->GetSpellClassOwners(
            spellId, player->GetUnlockedClassMask(), skillLineDummy);
        uint32 const benchedOwnerMask = unlockedOwnerMask & ~activeOwnerMask;

        // Active owners: track in the ledger.
        for (uint8 classId = 1; classId < MAX_CLASSES; ++classId)
            if (activeOwnerMask & (1u << (classId - 1)))
                (*ledger)[classId].insert(spellId);

        // Benched owners (unlocked but not active): bank into the remembered book. Never touch the ledger
        // for these -- SaveLedger rewrites `_spell` from the ledger per active class, so a ledgered benched
        // classId would be clobbered.
        uint32 const low = player->GetGUID().GetCounter();
        for (uint8 classId = 1; classId < MAX_CLASSES; ++classId)
        {
            if (!(benchedOwnerMask & (1u << (classId - 1))))
                continue;
            CharacterDatabase.Execute(Acore::StringFormat(
                "INSERT INTO `character_multiclass_spell` (`guid`, `classId`, `spellId`) VALUES ({}, {}, {})"
                " ON DUPLICATE KEY UPDATE `spellId` = `spellId`", low, classId, spellId));
        }

        // Pull the spell out of the live book only when it is owned solely by benched class(es); a spell
        // an active class also owns stays live for that class while still being banked for the benched one.
        if (benchedOwnerMask != 0 && activeOwnerMask == 0)
        {
            g_inOrchestration = true;
            player->removeSpell(spellId, SPEC_MASK_ALL, false);
            g_inOrchestration = false;
        }
    }

    void AttributeForgotSpell(Player* player, uint32 spellId)
    {
        if (g_inOrchestration)
            return;

        Ledger* ledger = FindLedger(player->GetGUID());
        if (!ledger)
            return;

        // Talent spells were never ledgered; skip for symmetry with AttributeLearnedSpell.
        if (IsTalentSpell(spellId))
            return;

        if (!IsClassAttributableSpell(spellId))
            return;  // professions are class-agnostic: never bank, ledger, or remove them

        bool skillLineDummy = false;
        uint32 const activeOwnerMask = player->GetSpellClassOwners(
            spellId, player->getClassMask(), skillLineDummy);
        for (uint8 classId = 1; classId < MAX_CLASSES; ++classId)
            if (activeOwnerMask & (1u << (classId - 1)))
            {
                auto itr = ledger->find(classId);
                if (itr != ledger->end())
                    itr->second.erase(spellId);
            }
    }

    void BackfillActiveLedgers(Player* player)
    {
        Ledger* ledger = FindLedger(player->GetGUID());
        if (!ledger)
            return;

        for (PlayerSpellMap::value_type const& pair : player->GetSpellMap())
        {
            if (pair.second->State == PLAYERSPELL_REMOVED)
                continue;
            uint32 const spellId = pair.first;
            // Talent spells are tracked by the core talent map, not the ledger.
            if (IsTalentSpell(spellId))
                continue;
            // Professions are class-agnostic; keep them out of the per-class ledger.
            if (!IsClassAttributableSpell(spellId))
                continue;
            // Resolve owners via the SkillRaceClassInfo path the trainer and learn-hook use -- a class
            // ability's ClassMask is frequently 0, so CombinedClassMask under-resolves it and would drop
            // those spells from the ledger, truncating the banked book on the next save.
            bool skillLineDummy = false;
            uint32 const activeOwnerMask = player->GetSpellClassOwners(
                spellId, player->getClassMask(), skillLineDummy);
            for (uint8 classId = 1; classId < MAX_CLASSES; ++classId)
                if (activeOwnerMask & (1u << (classId - 1)))
                    (*ledger)[classId].insert(spellId);
        }
    }

    void RouteExperience(Player* player, uint32 effectiveXp)
    {
        MulticlassProfile& mc = player->GetMulticlassProfile();
        uint8 const maxLevel = static_cast<uint8>(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
        auto const xpToNext = [](uint8 level) -> uint32 { return sObjectMgr->GetXPForLevel(level); };
        LocaleConstant const loc = player->GetSession()->GetSessionDbcLocale();

        // GetActiveClassesAtMinLevel is a snapshot of the pre-award receivers (a copy), so mutating the
        // profile inside the loop is safe; every tied-lowest active class gets the FULL award.
        for (uint8 classId : mc.GetActiveClassesAtMinLevel())
        {
            ClassProgress cp{ classId, mc.GetClassLevel(classId), mc.GetClassXp(classId) };
            if (ApplyXpToClass(cp, effectiveXp, maxLevel, xpToNext) > 0)
            {
                if (ChrClassesEntry const* ce = sChrClassesStore.LookupEntry(classId))
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "Your {} is now level {}!", ce->name[loc], uint32(cp.level));
            }
            mc.SetClassProgress(classId, cp.level, cp.xp);
        }

        // Always reconcile: even without a ding, the native XP bar must mirror the min class.
        ReconcileDisplayLevel(player);
    }

    void ReconcileDisplayLevel(Player* player)
    {
        MulticlassProfile& mc = player->GetMulticlassProfile();
        std::vector<uint8> const targets = mc.GetActiveClassesAtMinLevel();
        if (targets.empty())
            return;   // no active class -> leave the native level untouched

        uint8 const minLevel = mc.GetClassLevel(targets[0]);    // first active class at min (lowest slot)
        uint32 const displayXp = mc.GetClassXp(targets[0]);
        uint8 const cur = player->GetLevel();

        if (minLevel > cur)
        {
            // Step up one level at a time, matching native GiveXP's GiveLevel(level+1) loop so each
            // intermediate level's mail rewards / achievement criteria fire.
            for (uint8 lvl = cur + 1; lvl <= minLevel; ++lvl)
                player->GiveLevel(lvl);
        }
        else if (minLevel < cur)
        {
            // Deliberate de-level (fresh/lower class). Single call; stepping down would emit dozens of
            // packets. Cosmetic "level up" packet on a down-move is accepted.
            player->GiveLevel(minLevel);
        }

        player->SetUInt32Value(PLAYER_NEXT_LEVEL_XP, sObjectMgr->GetXPForLevel(minLevel));
        player->SetUInt32Value(PLAYER_XP, displayXp);
    }

    void SwapSlotClass(Player* player, uint8 slot, uint8 newClassId)
    {
        MulticlassProfile& mc = player->GetMulticlassProfile();
        Ledger& ledger = GetOrCreateLedger(player);
        std::vector<uint8> const active = mc.GetActiveClasses();   // snapshot before the swap
        uint8 const oldClassId = std::size_t(slot) < active.size() ? active[slot] : uint8(0);

        if (oldClassId != 0 && oldClassId != newClassId)
        {
            // Bank the outgoing class's book to `_spell` while it is still active, so a later
            // re-activation restores it. Synchronous: a rapid re-swap back to this class reads the book
            // via a sync query and must see committed rows, not an in-flight async write.
            SaveLedger(player, true);

            // Build the other still-active classes' ledgers (everything except this slot).
            std::vector<std::vector<uint32>> otherLedgers;
            for (uint8 cid : active)
            {
                if (cid == oldClassId)
                    continue;
                auto itr = ledger.find(cid);
                if (itr != ledger.end())
                    otherLedgers.emplace_back(itr->second.begin(), itr->second.end());
            }

            // Remove exactly the outgoing class's spells, keeping any a still-active class shares.
            g_inOrchestration = true;
            auto outItr = ledger.find(oldClassId);
            if (outItr != ledger.end())
                for (uint32 spellId : outItr->second)
                    if (!AnotherActiveClassOwns(spellId, otherLedgers))
                        player->removeSpell(spellId, SPEC_MASK_ALL, false);
            g_inOrchestration = false;

            ledger.erase(oldClassId);

            // Remove skill lines exclusive to the outgoing class. No privileged render/creation class:
            // a skill is kept only if another STILL-ACTIVE class owns it. Removing a skill line auto-drops
            // its general spells, so this stays under the same orchestration guard as the spell removal.
            g_inOrchestration = true;
            PlayerInfo const* info = sObjectMgr->GetPlayerInfo(player->getRace(), oldClassId);
            if (info)
            {
                for (PlayerCreateInfoSkill const& skill : info->skills)
                {
                    if (!GetSkillRaceClassInfo(skill.SkillId, player->getRace(), oldClassId))
                        continue;

                    bool sharedWithOtherActive = false;
                    for (uint8 cid : active)
                    {
                        if (cid == oldClassId)
                            continue;
                        if (GetSkillRaceClassInfo(skill.SkillId, player->getRace(), cid))
                        {
                            sharedWithOtherActive = true;
                            break;
                        }
                    }

                    if (!sharedWithOtherActive)
                        player->SetSkill(skill.SkillId, 0, 0, 0);
                }
            }
            g_inOrchestration = false;
        }

        ActivateClass(player, slot, newClassId);
        // Reconcile skills to the new active set BEFORE checking gear. A benched class's trained weapon
        // proficiency (e.g. 2H Swords) is not a starting skill and survives the removal above, so without
        // this the equippability check below still sees it and the item is only force-unequipped + mailed
        // on the next login. This mirrors the login-time skill prune, keeping the live swap == a relog.
        // Guarded like the other skill/spell removals so forgotten proficiency spells are not re-attributed.
        g_inOrchestration = true;
        player->PruneSkillsInvalidForActiveClasses();
        g_inOrchestration = false;
        // The active set — and thus weapon/armor proficiencies — is now final for this swap. Move any
        // gear the new set can no longer equip into the bags now, instead of leaving it equipped to be
        // force-unequipped and mailed on the next login.
        player->MoveUnusableEquippedItemsToInventory();
        ReconcileDisplayLevel(player);
        player->SyncMulticlassProjection();   // slot-0 may have changed -> reproject getClass()
        player->SaveMulticlassProfile();      // persist the new set (core-owned tables)
        SaveLedger(player);                   // persist the now-active classes' books
    }
}
