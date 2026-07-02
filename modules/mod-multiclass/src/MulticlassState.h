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

#ifndef MOD_MULTICLASS_STATE_H
#define MOD_MULTICLASS_STATE_H

#include "MulticlassLogic.h"
#include "ObjectGuid.h"
#include <array>
#include <unordered_map>
#include <unordered_set>

class Player;

namespace Multiclass
{
    // Per-character spell ledger: active classId -> learned class-specific spell IDs.
    // The class SET (owned pool + active + per-class level/xp) now lives in
    // Player::GetMulticlassProfile(); this module only tracks per-class spell banking
    // (character_multiclass_spell).
    using Ledger = std::unordered_map<uint8, std::unordered_set<uint32>>;

    Ledger& GetOrCreateLedger(Player* player);
    Ledger* FindLedger(ObjectGuid guid);
    void LoadLedger(Player* player);
    void SaveLedger(Player* player, bool sync = false);
    void UnloadLedger(Player* player);
    void UnlockClass(Player* player, uint8 classId);
    void ActivateClass(Player* player, uint8 slot, uint8 classId);
    void SwapSlotClass(Player* player, uint8 slot, uint8 newClassId);
    void ReconcileDisplayLevel(Player* player);
    void RouteExperience(Player* player, uint32 effectiveXp);
    void AttributeLearnedSpell(Player* player, uint32 spellId);
    void AttributeForgotSpell(Player* player, uint32 spellId);
    void BackfillActiveLedgers(Player* player);
}

#endif
