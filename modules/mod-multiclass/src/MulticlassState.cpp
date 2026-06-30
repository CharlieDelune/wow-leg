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
#include "DatabaseEnv.h"
#include "Player.h"
#include "StringFormat.h"
#include <unordered_map>

namespace
{
    std::unordered_map<ObjectGuid, Multiclass::PlayerState> g_states;
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

        CharacterDatabase.CommitTransaction(trans);
    }

    void UnloadState(ObjectGuid guid)
    {
        SaveState(guid);
        g_states.erase(guid);
    }
}
