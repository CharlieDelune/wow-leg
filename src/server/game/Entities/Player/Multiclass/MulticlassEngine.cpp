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

#include "MulticlassEngine.h"
#include "MulticlassClientProtocol.h"
#include "MulticlassLogic.h"
#include "MulticlassSpells.h"
#include "CellImpl.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GridNotifiers.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <unordered_map>

/// @todo: this import is not necessary for compilation and marked as unused by the IDE
//  however, for some reasons removing it would cause a damn linking issue
//  there is probably some underlying problem with imports which should properly addressed
//  see: https://github.com/azerothcore/azerothcore-wotlk/issues/9766
#include "GridNotifiersImpl.h"

// kSlotCapacityMax is hardcoded in the dependency-free MulticlassProfile header; assert it still tracks the
// absolute active-slot ceiling (MAX_CLASSES - 1) here, where SharedDefines.h makes MAX_CLASSES visible.
static_assert(MulticlassProfile::kSlotCapacityMax == MAX_CLASSES - 1,
    "kSlotCapacityMax must equal MAX_CLASSES - 1");

namespace
{
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
    // The class SET is loaded by core (Player::_LoadMulticlassProfile) before this runs; here we load
    // only the per-class learned-spell ledger, for the classes currently active in the profile.
    void LoadLedger(Player* player)
    {
        uint32 const low = player->GetGUID().GetCounter();
        Ledger& ledger = player->GetMulticlassLedger();
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

    // Persist only `character_multiclass_spell`, for the active classes. Core owns the set tables
    // (character_multiclass_class / _slot) via Player::_SaveMulticlassProfile.
    void SaveLedger(Player* player, bool sync)
    {
        Ledger& ledger = player->GetMulticlassLedger();

        uint32 const low = player->GetGUID().GetCounter();
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        for (uint8 classId : player->GetMulticlassProfile().GetActiveClasses())
        {
            // Skip a class absent from the in-memory ledger. During a multi-class SetActiveOrder the earlier
            // leavers are banked then erased from the map while still transiently in the active set, so
            // DELETE-ing here would permanently wipe their banked spellbook (a re-activation would restore
            // nothing). A class that legitimately knows no spells stays PRESENT with an empty set, so it is
            // not skipped and its "no rows" state still persists.
            auto itr = ledger.find(classId);
            if (itr == ledger.end())
                continue;

            trans->Append(Acore::StringFormat(
                "DELETE FROM `character_multiclass_spell` WHERE `guid` = {} AND `classId` = {}", low, classId));
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
        // Synchronous: a follow-up ActivateClass (same tick, e.g. `unlockclass` then `setslot`) reads
        // this kit back with a sync query and must see it committed, not an in-flight async write.
        CharacterDatabase.DirectCommitTransaction(trans);

        LOG_INFO("entities.player.multiclass", "UnlockClass: guid {} unlocked class {} with {} starter spells",
            low, classId, starters.size());
    }

    void ActivateClass(Player* player, uint8 slot, uint8 classId)
    {
        MulticlassProfile& mc = player->GetMulticlassProfile();
        Ledger& ledger = player->GetMulticlassLedger();
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
                LOG_INFO("entities.player.multiclass",
                    "ActivateClass: no creation spells for race {} class {}"
                    " (race-invalid combo); class starts spell-less",
                    player->getRace(), classId);
        }

        // Place the class at the requested slot (owned by now, in both branches). SetSlot fills the slot
        // whether it was empty or occupied; positions are stable, so this never shifts sibling slots.
        if (!mc.SetSlot(slot, classId))
        {
            // Defensive: the command path pre-validates slot/cap/duplicates, so this should not fire.
            // Bail before granting so an unvalidated future caller can never leave a class
            // learned-but-not-active (skills/spells granted while the active set stayed unchanged).
            LOG_ERROR("entities.player.multiclass", "ActivateClass: could not place class {} at slot {} for guid {}",
                classId, slot, low);
            return;
        }

        player->SetMulticlassInOrchestration(true);
        // Grant the class's skill lines (idempotent) so the client renders its abilities and trainers; runs
        // under the guard because SetSkill auto-learns each line's general spells, which the learn hook
        // would otherwise mis-attribute.
        GrantClassSkills(player, classId);
        for (uint32 spellId : spells)
            player->learnSpell(spellId, false);
        player->SetMulticlassInOrchestration(false);

        ledger[classId] = std::move(spells);

        // Bring the newly-active class's persisted talents live (auras + specMask bit 0). Guarded so the
        // talent-triggered spell grants are not re-attributed by the learn hook. A brand-new class has no
        // talents yet -> no-op. The projected view is refreshed by FinalizeActiveSetChange.
        player->SetMulticlassInOrchestration(true);
        player->ActivateClassTalents(classId);
        player->ActivateClassGlyphs(classId);
        player->SetMulticlassInOrchestration(false);
    }

    void AttributeLearnedSpell(Player* player, uint32 spellId)
    {
        if (player->IsMulticlassInOrchestration())
            return;

        Ledger& ledger = player->GetMulticlassLedger();

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
                ledger[classId].insert(spellId);

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
            player->SetMulticlassInOrchestration(true);
            player->removeSpell(spellId, SPEC_MASK_ALL, false);
            player->SetMulticlassInOrchestration(false);
        }
    }

    void AttributeForgotSpell(Player* player, uint32 spellId)
    {
        if (player->IsMulticlassInOrchestration())
            return;

        Ledger& ledger = player->GetMulticlassLedger();

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
                auto itr = ledger.find(classId);
                if (itr != ledger.end())
                    itr->second.erase(spellId);
            }
    }

    void BackfillActiveLedgers(Player* player)
    {
        Ledger& ledger = player->GetMulticlassLedger();

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
                    ledger[classId].insert(spellId);
        }
    }

    void RouteExperience(Player* player, uint32 effectiveXp)
    {
        MulticlassProfile& mc = player->GetMulticlassProfile();
        uint8 const maxLevel = static_cast<uint8>(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
        auto const xpToNext = [](uint8 level) -> uint32 { return sObjectMgr->GetXPForLevel(level); };
        LocaleConstant const loc = player->GetSession()->GetSessionDbcLocale();

        // Snapshot before routing: the projected class's own level and the native display level. The projected
        // talent-point view only needs a direct refresh when the projected class itself dinged AND the display
        // level stayed put (so ReconcileDisplayLevel's GiveLevel never fired to resend the packet for us).
        uint8 const projected = mc.GetProjectedClass();
        uint8 const projectedLevelBefore = projected != 0 ? mc.GetClassLevel(projected) : 0;
        uint8 const displayLevelBefore = player->GetLevel();

        // GetActiveClassesAtMinLevel is a snapshot of the pre-award receivers (a copy), so mutating the
        // profile inside the loop is safe; every tied-lowest active class gets the FULL award.
        bool leveled = false;
        for (uint8 classId : mc.GetActiveClassesAtMinLevel())
        {
            ClassProgress cp{ classId, mc.GetClassLevel(classId), mc.GetClassXp(classId) };
            if (ApplyXpToClass(cp, effectiveXp, maxLevel, xpToNext) > 0)
            {
                leveled = true;
                if (ChrClassesEntry const* ce = sChrClassesStore.LookupEntry(classId))
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "Your {} is now level {}!", ce->name[loc], uint32(cp.level));
            }
            mc.SetClassProgress(classId, cp.level, cp.xp);
        }

        // Always reconcile: even without a ding, the native XP bar must mirror the min class.
        ReconcileDisplayLevel(player);

        // Progression may have unlocked active-slot capacity. Keyed off furthest progression (highest owned
        // level), monotonic, and a no-op unless a threshold (level 5 / 10) was just crossed -- so the common
        // per-kill path is a cheap compare with no DB write.
        GrantSlotCapacity(player, MulticlassProfile::SlotCapacityForLevel(mc.GetMaxOwnedLevel()));

        // Refresh the projected class's talent-point register ONLY when it actually dinged this award while the
        // display level stayed put -- i.e. the projected class dinged while a tied-lowest sibling with less
        // within-level XP pinned the display level (minLevel == cur), so ReconcileDisplayLevel's GiveLevel ->
        // InitTalentForLevel never fired. Guarding here keeps the common per-kill path from emitting an
        // unsolicited SMSG_TALENTS_INFO on every XP tick (byte-vanilla: retail sends it only on ding) and
        // avoids a redundant second send on a normal ding (where GiveLevel already resent it).
        if (projected != 0 && mc.GetClassLevel(projected) != projectedLevelBefore
            && player->GetLevel() == displayLevelBefore)
            player->RecomputeProjectedTalentView();

        // A ding also changes what the class-swap panel shows -- per-class levels and (at level 5/10) the
        // active-slot cap -- so push a fresh snapshot to refresh an open panel live. Gated on an actual ding
        // so the common per-kill XP path doesn't emit an addon message every tick.
        if (leveled)
            SendClientState(player);
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

    namespace
    {
        // Strip everything exclusive to a class leaving the active set; shared by SwapSlotClass and
        // UnsetSlot. `activeSnapshot` is the active set BEFORE the removal -- a spell/skill is kept only if
        // another class that stays active owns it (no privileged render/creation class).
        void TeardownOutgoingClass(Player* player, uint8 oldClassId, std::vector<uint8> const& activeSnapshot)
        {
            Ledger& ledger = player->GetMulticlassLedger();

            // Bank the outgoing class's book to `_spell` while it is still active, so a later
            // re-activation restores it. Synchronous: a rapid re-swap back to this class reads the book
            // via a sync query and must see committed rows, not an in-flight async write.
            SaveLedger(player, true);

            // Build the other still-active classes' ledgers (everything except the outgoing class).
            std::vector<std::vector<uint32>> otherLedgers;
            for (uint8 cid : activeSnapshot)
            {
                if (cid == oldClassId)
                    continue;
                auto itr = ledger.find(cid);
                if (itr != ledger.end())
                    otherLedgers.emplace_back(itr->second.begin(), itr->second.end());
            }

            // Remove exactly the outgoing class's spells, keeping any a still-active class shares.
            player->SetMulticlassInOrchestration(true);
            auto outItr = ledger.find(oldClassId);
            if (outItr != ledger.end())
                for (uint32 spellId : outItr->second)
                    if (!AnotherActiveClassOwns(spellId, otherLedgers))
                        player->RemoveSpellAndPurgeRow(spellId);
            player->SetMulticlassInOrchestration(false);

            ledger.erase(oldClassId);

            // Remove skill lines exclusive to the outgoing class. No privileged render/creation class:
            // a skill is kept only if another STILL-ACTIVE class owns it. Removing a skill line auto-drops
            // its general spells, so this stays under the same orchestration guard as the spell removal.
            player->SetMulticlassInOrchestration(true);
            PlayerInfo const* info = sObjectMgr->GetPlayerInfo(player->getRace(), oldClassId);
            if (info)
            {
                for (PlayerCreateInfoSkill const& skill : info->skills)
                {
                    if (!GetSkillRaceClassInfo(skill.SkillId, player->getRace(), oldClassId))
                        continue;

                    bool sharedWithOtherActive = false;
                    for (uint8 cid : activeSnapshot)
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
            player->SetMulticlassInOrchestration(false);

            // Strip the outgoing class's talent auras but KEEP its rows (build banks for re-activation).
            // Guarded like the spell/skill removals so talent-triggered spell removes are not re-attributed.
            player->SetMulticlassInOrchestration(true);
            player->BenchClassTalents(oldClassId);
            player->BenchClassGlyphs(oldClassId);
            player->SetMulticlassInOrchestration(false);
        }

        // Tell the players who currently SEE this character about its new active set, so their composite
        // labels (tooltip/inspect/etc.) update live. Same audience that already gets its field updates.
        void PushPeerToObservers(Player* player)
        {
            if (!player->IsInWorld())
                return;
            std::vector<uint8> const& active = player->GetMulticlassProfile().GetActiveClasses();
            std::string const name = player->GetName();
            float const range = player->GetVisibilityRange();
            std::list<Player*> observers;
            Acore::AnyPlayerInObjectRangeCheck check(player, range);
            Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(player, observers, check);
            Cell::VisitObjects(player, searcher, range);
            for (Player* obs : observers)
                SendPeer(obs, name, active);
        }

        // Reconcile skills/gear/level/projection to the now-final active set and persist, then recompute the
        // combined stat sheet in place (preserving vitals). Shared by SwapSlotClass and UnsetSlot.
        void FinalizeActiveSetChange(Player* player)
        {
            // A newly-active Death Knight needs its rune structure before anything (this call or the next
            // regen tick) walks it; the login path only allocates runes for a DK that was active at load.
            player->EnsureRunesInitialized();
            // Reconcile skills AND skill-teaching spells to the new active set BEFORE checking gear, so a live
            // set change == a relog (mirrors the login-time _LoadSpells + _LoadSkills prunes). A benched
            // class's trained proficiency is not a starting line the teardown above removes, so without this
            // it survives: the teaching spell lingers in character_spell (flagged + deleted only on the next
            // login -- e.g. 674 Dual Wield), and a stale skill (e.g. 2H Swords) leaves gear to be force-
            // unequipped + mailed on the next login. Prune both: PruneSpells removes the invalid teaching
            // spells (and purges their character_spell rows), PruneSkills removes the invalid skill lines.
            // Guarded like the other removals so nothing is re-attributed to the outgoing class.
            player->SetMulticlassInOrchestration(true);
            player->PruneSpellsInvalidForActiveClasses();
            player->PruneSkillsInvalidForActiveClasses();
            player->SetMulticlassInOrchestration(false);
            // The active set -- and thus weapon/armor proficiencies -- is now final. Move any gear the new
            // set can no longer equip into the bags now, instead of leaving it equipped to be force-
            // unequipped and mailed on the next login.
            player->MoveUnusableEquippedItemsToInventory();
            ReconcileDisplayLevel(player);
            player->SyncMulticlassProjection();   // slot-0 may have changed -> reproject getClass()
            player->RecomputeProjectedTalentView(); // render the (possibly new) projected class's tree/points
            player->SaveMulticlassProfile();      // persist the new set (core-owned tables)
            SaveLedger(player);                   // persist the now-active classes' books
            // The active set (and thus the combined stat sheet) is now final. Recompute in place, preserving
            // vitals -- covers a same-level change, where ReconcileDisplayLevel/GiveLevel is a no-op and
            // nothing else would recompute. (A level-changing change already recomputed via GiveLevel; this
            // idempotent second pass is harmless.)
            player->RecalculateMulticlassStats();
            SendClientState(player);
            PushPeerToObservers(player);   // P4b: observers' composite labels update live on swap
        }
    }

    void SwapSlotClass(Player* player, uint8 slot, uint8 newClassId)
    {
        MulticlassProfile& mc = player->GetMulticlassProfile();
        std::vector<uint8> const active = mc.GetActiveClasses();   // snapshot before the swap
        uint8 const oldClassId = mc.GetClassAtSlot(slot);          // class currently in this slot (0 if empty)

        // Tear the outgoing class down only when it is genuinely leaving the active set: not a no-op
        // self-set on its own slot, and not filling an empty slot (oldClassId == 0). The teardown's
        // still-active check reads the pre-swap snapshot, which still includes oldClassId.
        if (oldClassId != 0 && oldClassId != newClassId)
            TeardownOutgoingClass(player, oldClassId, active);

        ActivateClass(player, slot, newClassId);
        FinalizeActiveSetChange(player);
    }

    void UnsetSlot(Player* player, uint8 slot)
    {
        MulticlassProfile& mc = player->GetMulticlassProfile();
        std::vector<uint8> const active = mc.GetActiveClasses();   // snapshot before the removal
        uint8 const oldClassId = mc.GetClassAtSlot(slot);          // class in this slot (0 if already empty)
        // Nothing to clear in an empty slot, and never empty the last filled slot -- the projection reads
        // the slot-order front, so a character must keep at least one active class. active is the compact
        // set of filled slots, so its size is the live count. The command layer reports both cases to the
        // user; re-checking here keeps an unvalidated future caller from clearing wrongly.
        if (oldClassId == 0 || active.size() <= 1)
            return;

        TeardownOutgoingClass(player, oldClassId, active);
        mc.ClearSlot(slot);                   // empty this slot in place (guaranteed to succeed by the guard)
        FinalizeActiveSetChange(player);
    }

    // Whole-set rewrite from the class panel: `order` is the new active set in slot order (slot 0 first),
    // already validated (owned, distinct, within cap, non-empty). Subsumes the panel's activate / bench /
    // promote / reorder in one atomic op — a permutation SwapSlotClass/UnsetSlot can't express.
    void SetActiveOrder(Player* player, std::vector<uint8> const& order)
    {
        MulticlassProfile& mc = player->GetMulticlassProfile();
        std::vector<uint8> const oldActive = mc.GetActiveClasses();   // snapshot before the change

        auto const inOrder = [&order](uint8 cid)
        { return std::find(order.begin(), order.end(), cid) != order.end(); };
        auto const wasActive = [&oldActive](uint8 cid)
        { return std::find(oldActive.begin(), oldActive.end(), cid) != oldActive.end(); };

        // Classes leaving the active set: bank their book, strip exclusive spells/skills, bench talents.
        // Reference is the pre-change snapshot, matching SwapSlotClass/UnsetSlot; a spell/skill shared only
        // between two co-leaving classes is caught by FinalizeActiveSetChange's prune, the same backstop the
        // single-delta paths already lean on.
        for (uint8 cid : oldActive)
            if (!inOrder(cid))
                TeardownOutgoingClass(player, cid, oldActive);

        // Place the whole new set at once — stayers may shift position, entering classes take their slot.
        // A one-shot rewrite avoids the transient duplicate-slot conflicts a sequence of SetSlot moves hits.
        mc.SetActiveOrder(order);

        // Classes entering the active set: learn/restore their kit. ActivateClass re-runs SetSlot, now
        // idempotent (the class already occupies that slot), so it performs only the learn/talent work.
        for (std::size_t i = 0; i < order.size(); ++i)
            if (!wasActive(order[i]))
                ActivateClass(player, static_cast<uint8>(i), order[i]);

        FinalizeActiveSetChange(player);
    }

    void EnforceActiveCapacity(Player* player)
    {
        MulticlassProfile& mc = player->GetMulticlassProfile();
        // Enforce the cap by SLOT POSITION, mirroring SetSlot's `index >= _maxActive` rejection and Load's
        // `i < _maxActive` trim: a class is over-cap when its slot INDEX is >= the effective cap, NOT merely
        // when the filled COUNT exceeds it (a positional hole can make the count fit while a class still sits
        // at an illegal index -- HighestActiveSlotAboveCap encodes that, count-based checks miss it). Bench
        // over-cap slots highest-first so the lowest-slot (projected) classes survive.
        bool relocated = false;
        for (;;)
        {
            int const overCap = mc.HighestActiveSlotAboveCap();   // re-evaluated each pass: UnsetSlot mutates
            if (overCap < 0)
                break;   // every filled slot is within [0, cap)

            if (mc.GetActiveClasses().size() > 1)
            {
                UnsetSlot(player, uint8(overCap));   // bench it (teardown banks the book, keeps it owned)
            }
            else
            {
                // The sole active class is stranded above the cap; benching it would strip the character's
                // only class (UnsetSlot refuses, which would spin this loop). Relocate it into range instead
                // -- it stays active (no teardown), only its slot changes -- then persist below.
                if (mc.CompactSoleActiveIntoCap())
                    relocated = true;
                break;
            }
        }
        if (relocated)
        {
            // The relocated class kept its identity (only its slot moved), so the projected classId is
            // unchanged and this Sync is a defensive no-op; the real persistence need is the moved slot row.
            player->SyncMulticlassProjection();
            player->SaveMulticlassProfile();
        }
    }

    void GrantSlotCapacity(Player* player, uint8 target)
    {
        // Monotonic ratchet (never lowers); persist only on an actual raise -- this runs on every XP award,
        // so the common no-raise path must stay a cheap compare with no DB write. The monotonic decision lives
        // on MulticlassProfile (pure, unit-tested); future purchase/quest sources call this same entry point.
        if (player->GetMulticlassProfile().RaiseUnlockedTo(target))
            player->SaveMulticlassProfile();
    }

    void SetManagedLevel(Player* player, uint8 level)
    {
        // GM level command applied to a managed character: set EVERY active class to the target level, so the
        // character genuinely is that level across the classes it plays and the native level -- derived from the
        // min active level -- stops snapping back on the next XP tick. Capacity re-grants and the panel refreshes.
        if (!sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE))
            return;
        MulticlassProfile& mc = player->GetMulticlassProfile();
        for (uint8 const classId : mc.GetActiveClasses())
            mc.SetClassProgress(classId, level, 0);
        GrantSlotCapacity(player, MulticlassProfile::SlotCapacityForLevel(mc.GetMaxOwnedLevel()));
        player->SaveMulticlassProfile();
        SendClientState(player);
    }

    void SetSlotCapacity(Player* player, uint8 n)
    {
        MulticlassProfile& mc = player->GetMulticlassProfile();
        // Absolute override (GM/testing): may raise OR lower. Lowering can strand active classes, so evict.
        mc.SetUnlockedSlots(n);            // clamped to 1..kSlotCapacityMax, recomputes the effective cap
        player->SaveMulticlassProfile();
        EnforceActiveCapacity(player);     // if the new cap is below the filled count, bench the excess
    }

    uint32 GlyphIdFromItem(ItemTemplate const* proto)
    {
        if (!proto)
            return 0;
        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            if (proto->Spells[i].SpellId <= 0)
                continue;
            SpellInfo const* si = sSpellMgr->GetSpellInfo(uint32(proto->Spells[i].SpellId));
            if (!si)
                continue;
            for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
                if (si->Effects[e].Effect == SPELL_EFFECT_APPLY_GLYPH)
                    return si->Effects[e].MiscValue;
        }
        return 0;
    }

    void SendClientState(Player* player)
    {
        if (!player || !player->GetSession())
            return;
        bool const enabled = sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE);
        std::string payload(kClientMsgTag);
        payload += SerializeStateSnapshot(player->GetMulticlassProfile(), enabled);
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, payload);
        player->GetSession()->SendPacket(&data);

        // Realm's diegetic marker word, so the client can expand the {mcU}/{mcL} markers in cached narrative
        // text (npc_text / quest-query / page_text). Its own message because the word may contain spaces.
        std::string wordPayload(kClientMsgTag);
        wordPayload += SerializeDiegeticWord(sWorld->getStringConfig(CONFIG_MULTICLASS_DIEGETIC_CLASS_NAME));
        WorldPacket wordData;
        ChatHandler::BuildChatPacket(wordData, CHAT_MSG_WHISPER, LANG_ADDON, player, player, wordPayload);
        player->GetSession()->SendPacket(&wordData);

        // P4/SP2: one talent snapshot per ACTIVE class (ranks + that class's own free pool) for the
        // multi-tree UI. Only when enabled, so a byte-vanilla client never receives these.
        if (enabled)
        {
            MulticlassProfile const& mc = player->GetMulticlassProfile();
            for (uint8 const classId : mc.GetActiveClasses())
            {
                uint32 const pool = TalentPointsForLevel(mc.GetClassLevel(classId));
                uint32 const spent = player->SpentTalentPointsForClass(classId);
                uint32 const freePoints = pool > spent ? pool - spent : 0u;
                std::string tPayload(kClientMsgTag);
                tPayload += SerializeClassTalents(classId, freePoints, player->GetClassTalentRanks(classId));
                WorldPacket tData;
                ChatHandler::BuildChatPacket(tData, CHAT_MSG_WHISPER, LANG_ADDON, player, player, tPayload);
                player->GetSession()->SendPacket(&tData);
            }

            // P4/SP3: one glyph snapshot per ACTIVE class — the 6 slots' socketed glyph spell ids + per-slot
            // enabled (unlocked for that class's own level). The custom Glyphs ring renders straight from this.
            for (uint8 const classId : mc.GetActiveClasses())
            {
                std::vector<std::pair<uint32, uint32>> slots;
                slots.reserve(MAX_GLYPH_SLOT_INDEX);
                uint8 const classLevel = mc.GetClassLevel(classId);
                for (uint8 slot = 0; slot < MAX_GLYPH_SLOT_INDEX; ++slot)
                {
                    uint32 spellId = 0;
                    if (uint32 const g = player->GetClassGlyph(classId, slot))
                        if (GlyphPropertiesEntry const* ge = sGlyphPropertiesStore.LookupEntry(g))
                            spellId = ge->SpellId;
                    uint8 const unlock = GlyphSlotUnlockLevel(slot);
                    uint32 const en = (unlock == 0 || classLevel >= unlock) ? 1u : 0u;
                    slots.emplace_back(spellId, en);
                }
                std::string gPayload(kClientMsgTag);
                gPayload += SerializeClassGlyphs(classId, slots);
                WorldPacket gData;
                ChatHandler::BuildChatPacket(gData, CHAT_MSG_WHISPER, LANG_ADDON, player, player, gPayload);
                player->GetSession()->SendPacket(&gData);
            }
        }
    }

    void SendPeer(Player* recipient, std::string_view name, std::vector<uint8> const& active)
    {
        if (!recipient || !recipient->GetSession())
            return;
        std::string payload = std::string(kClientMsgTag) + SerializePeer(name, active);
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, recipient, recipient, payload);
        recipient->GetSession()->SendPacket(&data);
    }

    bool ShouldDeclassify(Player const* player)
    {
        if (!player || !player->IsMulticlassManaged())
            return false;

        if (player->GetMulticlassProfile().GetActiveClasses().size() < 2)
            return false;

        return !sWorld->getStringConfig(CONFIG_MULTICLASS_DIEGETIC_CLASS_NAME).empty();
    }

    void DeclassifyFor(Player const* player, std::string& text)
    {
        if (ShouldDeclassify(player))
            DeclassifyText(text, sWorld->getStringConfig(CONFIG_MULTICLASS_DIEGETIC_CLASS_NAME));
    }

    void MarkClassToken(std::string& text)
    {
        if (!sWorld->getBoolConfig(CONFIG_MULTICLASS_ENABLE)
            || sWorld->getStringConfig(CONFIG_MULTICLASS_DIEGETIC_CLASS_NAME).empty())
            return;

        ReplaceClassToken(text, "{mcU}", "{mcL}");
    }
}
