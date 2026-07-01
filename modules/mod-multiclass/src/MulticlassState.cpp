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
#include "DatabaseEnv.h"
#include "Player.h"
#include "StringFormat.h"
#include <unordered_map>

namespace
{
    std::unordered_map<ObjectGuid, Multiclass::PlayerState> g_states;
    bool g_inOrchestration = false;  // suppress learn/forget capture during our own grants/removes
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
            "SELECT `slot`, `classId`, `unlocked` FROM `character_multiclass_slot` WHERE `guid` = {} ORDER BY `slot`", low)))
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
        }

        // Per-class progression
        if (QueryResult result = CharacterDatabase.Query(Acore::StringFormat(
            "SELECT `classId`, `level`, `xp` FROM `character_multiclass_class` WHERE `guid` = {}", low)))
        {
            do
            {
                Field* fields = result->Fetch();
                uint8 const classId = fields[0].Get<uint8>();
                uint8 const level = fields[1].Get<uint8>();
                uint32 const xp = fields[2].Get<uint32>();
                for (ClassProgress& slot : state.slots)
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
                "SELECT `spellId` FROM `character_multiclass_spell` WHERE `guid` = {} AND `classId` = {}", low, cp.classId)))
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
                "INSERT INTO `character_multiclass_slot` (`guid`, `slot`, `classId`, `unlocked`) VALUES ({}, {}, {}, {})",
                low, slot, cp.classId, state->unlocked[slot] ? 1 : 0));

            // Per-class progression is upserted independently so a benched class
            // (one no longer in any slot) keeps its remembered row instead of being pruned.
            if (cp.classId != 0)
                trans->Append(Acore::StringFormat(
                    "INSERT INTO `character_multiclass_class` (`guid`, `classId`, `level`, `xp`) VALUES ({}, {}, {}, {}) "
                    "ON DUPLICATE KEY UPDATE `level` = {}, `xp` = {}",
                    low, cp.classId, cp.level, cp.xp, cp.level, cp.xp));
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

    void ActivateClass(Player* player, uint8 slot, uint8 classId)
    {
        PlayerState& state = GetOrCreateState(player);
        uint32 const low = player->GetGUID().GetCounter();
        ClassProgress& cp = state.slots[slot];
        cp.classId = classId;

        std::unordered_set<uint32> spells;

        // Remembered class? Restore its level/xp and exact spellbook.
        if (QueryResult clsRow = CharacterDatabase.Query(Acore::StringFormat(
            "SELECT `level`, `xp` FROM `character_multiclass_class` WHERE `guid` = {} AND `classId` = {}", low, classId)))
        {
            Field* f = clsRow->Fetch();
            cp.level = f[0].Get<uint8>();
            cp.xp = f[1].Get<uint32>();

            if (QueryResult spellRows = CharacterDatabase.Query(Acore::StringFormat(
                "SELECT `spellId` FROM `character_multiclass_spell` WHERE `guid` = {} AND `classId` = {}", low, classId)))
            {
                do
                {
                    spells.insert(spellRows->Fetch()[0].Get<uint32>());
                } while (spellRows->NextRow());
            }
        }
        else
        {
            // First time this class is assigned: fresh start + creation loadout.
            cp.level = 1;
            cp.xp = 0;
            for (uint32 spellId : StartingSpellsFor(player->getRace(), classId))
                spells.insert(spellId);
        }

        g_inOrchestration = true;
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

        for (uint8 classId : ClaimingClasses(state->slots, CombinedClassMask(spellId)))
            state->ledger[classId].insert(spellId);
    }

    void AttributeForgotSpell(Player* player, uint32 spellId)
    {
        if (g_inOrchestration)
            return;

        PlayerState* state = FindState(player->GetGUID());
        if (!state)
            return;

        for (uint8 classId : ClaimingClasses(state->slots, CombinedClassMask(spellId)))
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
            for (uint8 classId : ClaimingClasses(state->slots, CombinedClassMask(spellId)))
                state->ledger[classId].insert(spellId);
        }
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
        }

        ActivateClass(player, slot, newClassId);
        SaveState(player->GetGUID());
    }
}
