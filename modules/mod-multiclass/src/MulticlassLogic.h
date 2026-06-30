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

#ifndef MOD_MULTICLASS_LOGIC_H
#define MOD_MULTICLASS_LOGIC_H

#include <array>
#include <cstdint>
#include "Define.h"

namespace Multiclass
{
    constexpr uint8 MAX_CLASS_SLOTS = 3;

    struct ClassProgress
    {
        uint8 classId = 0;   // 0 == empty slot
        uint8 level = 1;
        uint32 xp = 0;
    };

    using SlotArray = std::array<ClassProgress, MAX_CLASS_SLOTS>;

    inline bool IsValidClassId(uint8 classId)
    {
        // 3.3.5a classes 1..11, with 10 unused.
        return classId >= 1 && classId <= 11 && classId != 10;
    }

    inline uint8 ActiveCount(SlotArray const& slots)
    {
        uint8 count = 0;
        for (ClassProgress const& slot : slots)
            if (slot.classId != 0)
                ++count;
        return count;
    }

    inline uint8 ComputeDisplayLevel(SlotArray const& slots)
    {
        uint8 lowest = 0;
        for (ClassProgress const& slot : slots)
        {
            if (slot.classId == 0)
                continue;
            if (lowest == 0 || slot.level < lowest)
                lowest = slot.level;
        }
        return lowest == 0 ? uint8(1) : lowest;
    }

    inline uint32 PerClassXp(uint32 amount, uint8 activeCount, float share)
    {
        if (activeCount <= 1)
            return amount;
        if (share < 0.0f)
            share = 0.0f;
        if (share > 1.0f)
            share = 1.0f;
        float const divided = static_cast<float>(amount) / static_cast<float>(activeCount);
        float const full = static_cast<float>(amount);
        return static_cast<uint32>(divided + share * (full - divided) + 0.5f);
    }

    inline uint32 TalentPointsForLevel(uint8 level)
    {
        return level < 10 ? 0u : static_cast<uint32>(level - 9);
    }

    inline bool CanAssignClass(SlotArray const& slots, uint8 slot, uint8 classId)
    {
        if (slot >= MAX_CLASS_SLOTS)
            return false;
        if (!IsValidClassId(classId))
            return false;
        for (uint8 i = 0; i < MAX_CLASS_SLOTS; ++i)
            if (i != slot && slots[i].classId == classId)
                return false;
        return true;
    }
}

#endif
