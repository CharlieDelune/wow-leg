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
#include <vector>
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

    // Minimum level among active (classId != 0) slots; 0 if no class is active.
    // Companion to ComputeDisplayLevel: this returns 0 for "no active class" so the
    // XP router can tell that apart from a genuine level 1.
    inline uint8 MinActiveLevel(SlotArray const& slots)
    {
        uint8 lowest = 0;
        for (ClassProgress const& slot : slots)
        {
            if (slot.classId == 0)
                continue;
            if (lowest == 0 || slot.level < lowest)
                lowest = slot.level;
        }
        return lowest;
    }

    // Slot indices (0..2) of the active classes tied at MinActiveLevel(slots), ascending.
    // These are the catch-up receivers: each gains the full XP award. Empty if none active.
    inline std::vector<uint8> SlotsAtMinLevel(SlotArray const& slots)
    {
        std::vector<uint8> result;
        uint8 const minLevel = MinActiveLevel(slots);
        if (minLevel == 0)
            return result;
        for (uint8 i = 0; i < MAX_CLASS_SLOTS; ++i)
            if (slots[i].classId != 0 && slots[i].level == minLevel)
                result.push_back(i);
        return result;
    }

    // Apply `award` XP to one class, leveling against `xpToNext`, capped at `maxLevel`.
    // xpToNext(L) returns the XP required to advance FROM level L TO L+1. At the cap no XP
    // is retained (mirrors the native engine stopping at max level). Returns levels gained.
    // CurveFn is any callable uint32(uint8) so tests can inject a synthetic curve.
    template <typename CurveFn>
    inline uint8 ApplyXpToClass(ClassProgress& cp, uint32 award, uint8 maxLevel, CurveFn xpToNext)
    {
        if (cp.level >= maxLevel)
            return 0;

        uint8 gained = 0;
        uint64 xp = static_cast<uint64>(cp.xp) + award;
        while (cp.level < maxLevel)
        {
            uint32 const need = xpToNext(cp.level);
            if (need == 0 || xp < need)
                break;
            xp -= need;
            ++cp.level;
            ++gained;
        }
        cp.xp = (cp.level >= maxLevel) ? 0u : static_cast<uint32>(xp);
        return gained;
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

    // Which currently-active classes own a spell, given the OR of its SkillLineAbility
    // class masks (0 = no class-specific entry => general/profession/racial => owned by none).
    // Owners are returned in slot order (slot 0 first); tests rely on this deterministic ordering.
    inline std::vector<uint8> ClaimingClasses(SlotArray const& slots, uint32 combinedClassMask)
    {
        std::vector<uint8> result;
        if (combinedClassMask == 0)
            return result;
        for (ClassProgress const& cp : slots)
        {
            if (cp.classId == 0)
                continue;
            if (combinedClassMask & (1u << (cp.classId - 1)))
                result.push_back(cp.classId);
        }
        return result;
    }

    // Swap-out guard: true if any OTHER active class's ledger still contains the spell,
    // meaning it must stay on the player rather than be removed.
    inline bool AnotherActiveClassOwns(uint32 spellId, std::vector<std::vector<uint32>> const& otherActiveClassLedgers)
    {
        for (std::vector<uint32> const& ledger : otherActiveClassLedgers)
            for (uint32 owned : ledger)
                if (owned == spellId)
                    return true;
        return false;
    }
}

#endif
