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

class Player;

namespace Multiclass
{
    struct PlayerState
    {
        SlotArray slots{};
        std::array<bool, MAX_CLASS_SLOTS> unlocked{ { true, false, false } };
        uint8 renderClass = 0;
    };

    PlayerState& GetOrCreateState(Player* player);
    PlayerState* FindState(ObjectGuid guid);
    void LoadState(Player* player);
    void SaveState(ObjectGuid guid);
    void UnloadState(ObjectGuid guid);
}

#endif
