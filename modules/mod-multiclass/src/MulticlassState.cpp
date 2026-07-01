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
    std::unordered_map<ObjectGuid, Multiclass::PlayerState> g_states;
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
    PlayerState* FindState(ObjectGuid guid)
    {
        auto itr = g_states.find(guid);
        return itr == g_states.end() ? nullptr : &itr->second;
    }

    void LoadState(Player* player)
    {
        ObjectGuid const guid = player->GetGUID();
        uint32 const low = guid.GetCounter();

        PlayerState& state = g_states[guid];
        state = PlayerState{};
        state.renderClass = player->getClass();

        // Slots
        if (QueryResult result = CharacterDatabase.Query(Acore::StringFormat(
            "SELECT `slot`, `classId`, `unlocked` FROM `character_multiclass_slot` WHERE `guid` = {} ORDER BY `slot`",
            low)))
        {
            do
            {
                Field* fields = result->Fetch();
                uint8 const slot = fields[0].Get<uint8>();
                if (slot >= MAX_CLASS_SLOTS)
                    continue;
                state.slots[slot].classId = fields[1].Get<uint8>();
                state.unlocked[slot] = fields[2].Get<uint8>() != 0;
            } while (result->NextRow());
        }
        else
        {
            // First login under multiclass: seed slot 0 from the character's current class.
            state.slots[0].classId = player->getClass();
            state.slots[0].level = player->GetLevel();
            state.slots[0].xp = player->GetUInt32Value(PLAYER_XP);
            state.unlocked[0] = true;
            state.pool[state.slots[0].classId] = state.slots[0];
        }

        // Per-class progression
        if (QueryResult result = CharacterDatabase.Query(Acore::StringFormat(
            "SELECT `classId`, `level`, `xp` FROM `character_multiclass_class` WHERE `guid` = {}", low)))
        {
            do
            {
                Field* fields = result->Fetch();
                uint8 const classId = fields[0].Get<uint8>();
                uint8 const level   = fields[1].Get<uint8>();
                uint32 const xp     = fields[2].Get<uint32>();
                state.pool[classId] = ClassProgress{ classId, level, xp };  // full unlocked pool
                for (ClassProgress& slot : state.slots)                      // active slots mirror the pool
                    if (slot.classId == classId)
                    {
                        slot.level = level;
                        slot.xp = xp;
                    }
            } while (result->NextRow());
        }

        // Per-class learned-spell ledger (only for classes currently in a slot).
        for (ClassProgress const& cp : state.slots)
        {
            if (cp.classId == 0)
                continue;
            if (QueryResult result = CharacterDatabase.Query(Acore::StringFormat(
                "SELECT `spellId` FROM `character_multiclass_spell` WHERE `guid` = {} AND `classId` = {}",
                low, cp.classId)))
            {
                do
                {
                    state.ledger[cp.classId].insert(result->Fetch()[0].Get<uint32>());
                } while (result->NextRow());
            }
        }
    }

    PlayerState& GetOrCreateState(Player* player)
    {
        if (PlayerState* existing = FindState(player->GetGUID()))
            return *existing;
        LoadState(player);
        return g_states[player->GetGUID()];
    }

    void SaveState(ObjectGuid guid)
    {
        PlayerState* state = FindState(guid);
        if (!state)
            return;

        uint32 const low = guid.GetCounter();
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        // Slots are a fixed 0..2 set and always fully rewritten.
        trans->Append(Acore::StringFormat("DELETE FROM `character_multiclass_slot` WHERE `guid` = {}", low));

        for (uint8 slot = 0; slot < MAX_CLASS_SLOTS; ++slot)
        {
            ClassProgress const& cp = state->slots[slot];
            trans->Append(Acore::StringFormat(
                "INSERT INTO `character_multiclass_slot` (`guid`, `slot`, `classId`, `unlocked`)"
                " VALUES ({}, {}, {}, {})",
                low, slot, cp.classId, state->unlocked[slot] ? 1 : 0));

            // Per-class progression is upserted independently so a benched class
            // (one no longer in any slot) keeps its remembered row instead of being pruned.
            if (cp.classId != 0)
            {
                trans->Append(Acore::StringFormat(
                    "INSERT INTO `character_multiclass_class` (`guid`, `classId`, `level`, `xp`)"
                    " VALUES ({}, {}, {}, {}) ON DUPLICATE KEY UPDATE `level` = {}, `xp` = {}",
                    low, cp.classId, cp.level, cp.xp, cp.level, cp.xp));
                state->pool[cp.classId] = cp;  // keep the in-memory unlocked pool level in sync with the active slot
            }
        }

        for (uint8 slot = 0; slot < MAX_CLASS_SLOTS; ++slot)
        {
            uint8 const classId = state->slots[slot].classId;
            if (classId == 0)
                continue;

            trans->Append(Acore::StringFormat(
                "DELETE FROM `character_multiclass_spell` WHERE `guid` = {} AND `classId` = {}", low, classId));

            auto itr = state->ledger.find(classId);
            if (itr != state->ledger.end())
                for (uint32 spellId : itr->second)
                    trans->Append(Acore::StringFormat(
                        "INSERT INTO `character_multiclass_spell` (`guid`, `classId`, `spellId`) VALUES ({}, {}, {})",
                        low, classId, spellId));
        }

        CharacterDatabase.CommitTransaction(trans);
    }

    void UnloadState(ObjectGuid guid)
    {
        SaveState(guid);
        g_states.erase(guid);
    }

    void UnlockClass(Player* player, uint8 classId)
    {
        if (classId == 0 || classId >= MAX_CLASSES)
            return;

        PlayerState& state = GetOrCreateState(player);
        if (state.pool.find(classId) != state.pool.end())
            return;  // already unlocked -> idempotent

        uint32 const low = player->GetGUID().GetCounter();
        state.pool[classId] = ClassProgress{ classId, 1, 0 };

        // Bank the creation kit into the class's remembered book (not live -- the class is benched
        // until it is slotted; its skill lines and spells go live only via ActivateClass). The `_class`
        // row and every `_spell` row commit as ONE transaction so ActivateClass's benched-restore can
        // never read a half-written kit.
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        trans->Append(Acore::StringFormat(
            "INSERT INTO `character_multiclass_class` (`guid`, `classId`, `level`, `xp`) VALUES ({}, {}, 1, 0)"
            " ON DUPLICATE KEY UPDATE `classId` = `classId`", low, classId));
        auto const starters = StartingSpellsFor(player, classId);
        for (uint32 spellId : starters)
            trans->Append(Acore::StringFormat(
                "INSERT INTO `character_multiclass_spell` (`guid`, `classId`, `spellId`) VALUES ({}, {}, {})"
                " ON DUPLICATE KEY UPDATE `spellId` = `spellId`", low, classId, spellId));
        CharacterDatabase.CommitTransaction(trans);

        LOG_INFO("module.multiclass", "UnlockClass: guid {} unlocked class {} with {} starter spells",
            low, classId, starters.size());
    }

    void ActivateClass(Player* player, uint8 slot, uint8 classId)
    {
        PlayerState& state = GetOrCreateState(player);
        uint32 const low = player->GetGUID().GetCounter();
        ClassProgress& cp = state.slots[slot];
        cp.classId = classId;

        std::unordered_set<uint32> spells;

        // A COMMITTED `_class` row (unlocked in a prior call/session, or previously slotted) means the
        // class is remembered: restore its level/xp and exact spellbook from the DB. We decide on the
        // committed row -- not pool membership -- precisely to dodge the async-write race: UnlockClass
        // persists via CharacterDatabase.Execute (asynchronous), so a brand-new class's rows are NOT yet
        // committed here; reading them back would restore an empty spellbook. Taking the else branch is
        // proof no committed row exists, so we learn the kit from StartingSpellsFor in-memory instead.
        if (QueryResult clsRow = CharacterDatabase.Query(Acore::StringFormat(
            "SELECT `level`, `xp` FROM `character_multiclass_class` WHERE `guid` = {} AND `classId` = {}",
            low, classId)))
        {
            Field* f = clsRow->Fetch();
            cp.level = f[0].Get<uint8>();
            cp.xp = f[1].Get<uint32>();

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
            // First time this class is assigned: unlock it (create the `_class` row, bank the kit into
            // `_spell`, add the pool entry) then start fresh with the creation loadout. Learn that kit
            // LIVE from StartingSpellsFor in-memory -- never from a read-back of UnlockClass's async
            // writes, which may not have committed yet (see the branch note above).
            UnlockClass(player, classId);
            cp.level = 1;
            cp.xp = 0;
            for (uint32 spellId : StartingSpellsFor(player, classId))
                spells.insert(spellId);
            if (spells.empty())
                LOG_INFO("module.multiclass",
                    "ActivateClass: no creation spells for race {} class {}"
                    " (race-invalid combo); class starts spell-less",
                    player->getRace(), classId);
        }

        g_inOrchestration = true;
        // Grant the class's skill lines (idempotent, both branches) so the client renders its
        // abilities and trainers; must run under the guard because SetSkill auto-learns each skill
        // line's general spells, which would otherwise be mis-attributed by the learn hook.
        GrantClassSkills(player, classId);
        for (uint32 spellId : spells)
            player->learnSpell(spellId, false);
        g_inOrchestration = false;

        state.ledger[classId] = std::move(spells);
    }

    void AttributeLearnedSpell(Player* player, uint32 spellId)
    {
        if (g_inOrchestration)
            return;

        PlayerState* state = FindState(player->GetGUID());
        if (!state)
            return;

        // Talent spells are tracked by the core talent map, not the ledger.
        if (IsTalentSpell(spellId))
            return;

        if (!IsClassAttributableSpell(spellId))
            return;  // professions are class-agnostic: never bank, ledger, or remove them

        bool skillLineDummy = false;
        uint32 const activeOwnerMask = player->GetSpellClassOwners(
            spellId, player->GetEffectiveClassMask(), skillLineDummy);
        uint32 const unlockedOwnerMask = player->GetSpellClassOwners(
            spellId, player->GetUnlockedClassMask(), skillLineDummy);
        uint32 const benchedOwnerMask = unlockedOwnerMask & ~activeOwnerMask;

        // Active owners: track in the ledger exactly as before.
        for (uint8 classId = 1; classId < MAX_CLASSES; ++classId)
            if (activeOwnerMask & (1u << (classId - 1)))
                state->ledger[classId].insert(spellId);

        // Benched owners (unlocked but not active): bank into the remembered book. Never touch
        // state->ledger for these -- SaveState rewrites `character_multiclass_spell` from the ledger per
        // active slot, so a ledgered benched classId would be clobbered.
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

        PlayerState* state = FindState(player->GetGUID());
        if (!state)
            return;

        // Talent spells were never ledgered; skip for symmetry with AttributeLearnedSpell.
        if (IsTalentSpell(spellId))
            return;

        if (!IsClassAttributableSpell(spellId))
            return;  // professions are class-agnostic: never bank, ledger, or remove them

        bool skillLineDummy = false;
        uint32 const activeOwnerMask = player->GetSpellClassOwners(
            spellId, player->GetEffectiveClassMask(), skillLineDummy);
        for (uint8 classId = 1; classId < MAX_CLASSES; ++classId)
            if (activeOwnerMask & (1u << (classId - 1)))
            {
                auto itr = state->ledger.find(classId);
                if (itr != state->ledger.end())
                    itr->second.erase(spellId);
            }
    }

    void BackfillActiveLedgers(Player* player)
    {
        PlayerState* state = FindState(player->GetGUID());
        if (!state)
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
            // Resolve owners via the SkillRaceClassInfo path the trainer and learn-hook use -- a
            // class ability's ClassMask is frequently 0, so CombinedClassMask under-resolves it and
            // would drop those spells from the ledger, truncating the banked book on the next save.
            bool skillLineDummy = false;
            uint32 const activeOwnerMask = player->GetSpellClassOwners(
                spellId, player->GetEffectiveClassMask(), skillLineDummy);
            for (uint8 classId = 1; classId < MAX_CLASSES; ++classId)
                if (activeOwnerMask & (1u << (classId - 1)))
                    state->ledger[classId].insert(spellId);
        }
    }

    void RouteExperience(Player* player, uint32 effectiveXp)
    {
        PlayerState* state = FindState(player->GetGUID());
        if (!state)
            return;

        uint8 const maxLevel = static_cast<uint8>(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
        auto const xpToNext = [](uint8 level) -> uint32 { return sObjectMgr->GetXPForLevel(level); };
        LocaleConstant const loc = player->GetSession()->GetSessionDbcLocale();

        // Snapshot of the catch-up receivers is taken once, here (SlotsAtMinLevel reads the
        // pre-award levels); every tied-lowest class gets the FULL award.
        for (uint8 slotIdx : SlotsAtMinLevel(state->slots))
        {
            ClassProgress& cp = state->slots[slotIdx];
            if (ApplyXpToClass(cp, effectiveXp, maxLevel, xpToNext) > 0)
            {
                if (ChrClassesEntry const* ce = sChrClassesStore.LookupEntry(cp.classId))
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "Your {} is now level {}!", ce->name[loc], uint32(cp.level));
            }
        }

        // Always reconcile: even without a ding, the native XP bar must mirror the min class.
        ReconcileDisplayLevel(player);
    }

    void ReconcileDisplayLevel(Player* player)
    {
        PlayerState* state = FindState(player->GetGUID());
        if (!state)
            return;

        std::vector<uint8> const targets = SlotsAtMinLevel(state->slots);
        if (targets.empty())
            return;   // no active class -> leave the native level untouched

        uint8 const minLevel = state->slots[targets[0]].level;
        uint32 const displayXp = state->slots[targets[0]].xp;   // lowest slot index at min
        uint8 const cur = player->GetLevel();

        if (minLevel > cur)
        {
            // Step up one level at a time, matching native GiveXP's GiveLevel(level+1) loop
            // so each intermediate level's mail rewards / achievement criteria fire.
            for (uint8 lvl = cur + 1; lvl <= minLevel; ++lvl)
                player->GiveLevel(lvl);
        }
        else if (minLevel < cur)
        {
            // Deliberate de-level (fresh/lower slot). Single call; stepping down would emit
            // dozens of packets. Cosmetic "level up" packet on a down-move is accepted.
            player->GiveLevel(minLevel);
        }

        player->SetUInt32Value(PLAYER_NEXT_LEVEL_XP, sObjectMgr->GetXPForLevel(minLevel));
        player->SetUInt32Value(PLAYER_XP, displayXp);
    }

    void SwapSlotClass(Player* player, uint8 slot, uint8 newClassId)
    {
        PlayerState& state = GetOrCreateState(player);
        uint8 const oldClassId = state.slots[slot].classId;

        if (oldClassId != 0 && oldClassId != newClassId)
        {
            // Persist the outgoing class's current state so its benched row is up to date.
            SaveState(player->GetGUID());

            // Build the other still-active classes' ledgers (everything except this slot).
            std::vector<std::vector<uint32>> otherLedgers;
            for (uint8 s = 0; s < MAX_CLASS_SLOTS; ++s)
            {
                if (s == slot)
                    continue;
                uint8 const cid = state.slots[s].classId;
                if (cid == 0)
                    continue;
                auto itr = state.ledger.find(cid);
                if (itr != state.ledger.end())
                    otherLedgers.emplace_back(itr->second.begin(), itr->second.end());
            }

            // Remove exactly the outgoing class's spells, keeping any a still-active class shares.
            g_inOrchestration = true;
            auto outItr = state.ledger.find(oldClassId);
            if (outItr != state.ledger.end())
                for (uint32 spellId : outItr->second)
                    if (!AnotherActiveClassOwns(spellId, otherLedgers))
                        player->removeSpell(spellId, SPEC_MASK_ALL, false);
            g_inOrchestration = false;

            state.ledger.erase(oldClassId);

            // Remove skill lines exclusive to the outgoing class (mirror the spell "another
            // active class owns it" guard). Removing a skill line auto-drops its general
            // spells, so this stays under the same orchestration guard as the spell removal.
            g_inOrchestration = true;
            PlayerInfo const* info = sObjectMgr->GetPlayerInfo(player->getRace(), oldClassId);
            if (info)
            {
                for (PlayerCreateInfoSkill const& skill : info->skills)
                {
                    if (!GetSkillRaceClassInfo(skill.SkillId, player->getRace(), oldClassId))
                        continue;

                    bool sharedWithActiveOrRender =
                        GetSkillRaceClassInfo(skill.SkillId, player->getRace(), state.renderClass) != nullptr;
                    for (uint8 s = 0; !sharedWithActiveOrRender && s < MAX_CLASS_SLOTS; ++s)
                    {
                        if (s == slot)
                            continue;
                        uint8 const cid = state.slots[s].classId;
                        if (cid != 0 && GetSkillRaceClassInfo(skill.SkillId, player->getRace(), cid))
                            sharedWithActiveOrRender = true;
                    }

                    if (!sharedWithActiveOrRender)
                        player->SetSkill(skill.SkillId, 0, 0, 0);
                }
            }
            g_inOrchestration = false;
        }

        ActivateClass(player, slot, newClassId);
        ReconcileDisplayLevel(player);
        SaveState(player->GetGUID());
    }
}
