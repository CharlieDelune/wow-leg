/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ACORE_MULTICLASS_PROFILE_H
#define ACORE_MULTICLASS_PROFILE_H

#include "Define.h"
#include <algorithm>
#include <vector>

// A character's class-set: the owned pool of classes (each with its own level/xp) plus the
// ordered subset that is currently active. active[0] is the projected class (the single class
// the client wire renders). Pure data structure — no Player/DB/config dependency. Player owns
// one (P1b); the persistence, projection, and class-check layers drive it.
class MulticlassProfile
{
public:
    struct ClassProgress
    {
        uint8 classId = 0;   // 1..11 (10 unused); 0 == invalid/empty
        uint8 level   = 1;
        uint32 xp     = 0;
    };

    // classIds follow WoW: 1..11 with 10 unused (CLASS_NONE == 0).
    [[nodiscard]] static bool IsValidClassId(uint8 classId)
    {
        return classId >= 1 && classId <= 11 && classId != 10;
    }

    // ---- owned pool ----
    [[nodiscard]] bool HasOwnedClass(uint8 classId) const { return FindOwned(classId) != nullptr; }

    [[nodiscard]] uint8 GetClassLevel(uint8 classId) const
    {
        ClassProgress const* cp = FindOwned(classId);
        return cp ? cp->level : uint8(0);
    }

    [[nodiscard]] std::vector<uint8> GetOwnedClasses() const
    {
        std::vector<uint8> out;
        out.reserve(_pool.size());
        for (ClassProgress const& cp : _pool)
            out.push_back(cp.classId);
        return out;
    }

    bool AddOwnedClass(uint8 classId, uint8 level = 1, uint32 xp = 0)
    {
        if (!IsValidClassId(classId) || HasOwnedClass(classId))
            return false;
        _pool.push_back(ClassProgress{ classId, level, xp });
        return true;
    }

    bool SetClassProgress(uint8 classId, uint8 level, uint32 xp)
    {
        ClassProgress* cp = FindOwned(classId);
        if (!cp)
            return false;
        cp->level = level;
        cp->xp = xp;
        return true;
    }

    // ---- active set (positional slots) ----
    // Slots are stable numbered positions: `_slots[i]` is the class in slot i (0 == empty), and clearing a
    // slot never shifts the others. `_active` is a derived cache -- the non-empty classIds in slot order --
    // rebuilt on every slot mutation, so consumers that just want "the set of active classes" (the stat
    // combine, the masks, the projection) keep reading a compact list at zero per-call cost. The projected
    // class is the lowest-numbered filled slot (`_active.front()`).
    [[nodiscard]] std::vector<uint8> const& GetActiveClasses() const { return _active; }

    // The positional view: index == slot, value == classId (0 == empty). May hold interior/trailing holes.
    [[nodiscard]] std::vector<uint8> const& GetSlots() const { return _slots; }

    [[nodiscard]] uint8 GetClassAtSlot(uint8 index) const
    {
        return std::size_t(index) < _slots.size() ? _slots[index] : uint8(0);
    }

    [[nodiscard]] uint8 GetProjectedClass() const { return _active.empty() ? uint8(0) : _active.front(); }

    [[nodiscard]] bool HasActiveClass(uint8 classId) const
    {
        return std::find(_active.begin(), _active.end(), classId) != _active.end();
    }

    [[nodiscard]] uint32 GetActiveClassMask() const
    {
        uint32 mask = 0;
        for (uint8 classId : _active)          // the cache holds only non-zero classIds
            mask |= 1u << (classId - 1);
        return mask;
    }

    // Put an owned class into slot `index` (valid range 0.._maxActive-1), replacing whatever occupied it.
    // Lower slots may stay empty — positions are stable, never compacted. Rejects an out-of-range slot, an
    // invalid/unowned class, or a class already active in a DIFFERENT slot. A displaced class stays owned.
    bool SetSlot(uint8 index, uint8 newClassId)
    {
        if (index >= _maxActive)
            return false;
        if (!IsValidClassId(newClassId) || !HasOwnedClass(newClassId))
            return false;
        for (std::size_t i = 0; i < _slots.size(); ++i)
            if (i != std::size_t(index) && _slots[i] == newClassId)
                return false;
        if (std::size_t(index) >= _slots.size())
            _slots.resize(std::size_t(index) + 1, uint8(0));
        _slots[index] = newClassId;
        RebuildActiveCache();
        return true;
    }

    // Empty slot `index`; the class there stays owned (benched). Positions do not shift. Refuses to clear
    // the last filled slot — a managed character must always keep at least one active class, since the
    // projection reads the slot-order front.
    bool ClearSlot(uint8 index)
    {
        if (std::size_t(index) >= _slots.size() || _slots[index] == 0)
            return false;
        if (_active.size() <= 1)
            return false;
        _slots[index] = 0;
        RebuildActiveCache();
        return true;
    }

    // Reset and repopulate from persisted data. `pool` is every owned class; `positionalSlots[i]` is the
    // classId in slot i (0 == empty), exactly as stored — interior holes are preserved. Each non-empty
    // entry must be owned; entries at or beyond the current cap, and duplicates, are dropped (benched).
    void Load(std::vector<ClassProgress> const& pool, std::vector<uint8> const& positionalSlots)
    {
        _pool.clear();
        _slots.clear();
        _active.clear();
        for (ClassProgress const& cp : pool)
            AddOwnedClass(cp.classId, cp.level, cp.xp);
        for (std::size_t i = 0; i < positionalSlots.size() && i < _maxActive; ++i)
        {
            uint8 const classId = positionalSlots[i];
            if (classId == 0 || !HasOwnedClass(classId))
                continue;
            bool duplicate = false;
            for (uint8 placed : _slots)
                if (placed == classId)
                {
                    duplicate = true;
                    break;
                }
            if (duplicate)
                continue;
            if (i >= _slots.size())
                _slots.resize(i + 1, uint8(0));
            _slots[i] = classId;
        }
        RebuildActiveCache();
    }

    [[nodiscard]] uint32 GetOwnedClassMask() const
    {
        uint32 mask = 0;
        for (ClassProgress const& cp : _pool)
            if (cp.classId != 0)
                mask |= 1u << (cp.classId - 1);
        return mask;
    }

    [[nodiscard]] uint32 GetClassXp(uint8 classId) const
    {
        ClassProgress const* cp = FindOwned(classId);
        return cp ? cp->xp : 0u;
    }

    [[nodiscard]] uint8 GetMinActiveLevel() const
    {
        uint8 minLevel = 0;
        for (uint8 classId : _active)
        {
            uint8 const lvl = GetClassLevel(classId);
            if (minLevel == 0 || lvl < minLevel)
                minLevel = lvl;
        }
        return minLevel;
    }

    [[nodiscard]] std::vector<uint8> GetActiveClassesAtMinLevel() const
    {
        std::vector<uint8> out;
        uint8 const minLevel = GetMinActiveLevel();
        if (minLevel == 0)
            return out;
        for (uint8 classId : _active)
            if (GetClassLevel(classId) == minLevel)
                out.push_back(classId);
        return out;
    }

    // ---- active cap ----
    [[nodiscard]] uint8 GetMaxActiveClasses() const { return _maxActive; }
    void SetMaxActiveClasses(uint8 maxActive) { _maxActive = maxActive < 1 ? uint8(1) : maxActive; }

private:
    // Rebuild the derived compact cache (non-empty classIds in slot order) from the positional slots.
    // Called after every slot mutation so GetActiveClasses()/GetProjectedClass() stay a zero-cost read.
    void RebuildActiveCache()
    {
        _active.clear();
        for (uint8 classId : _slots)
            if (classId != 0)
                _active.push_back(classId);
    }

    [[nodiscard]] ClassProgress const* FindOwned(uint8 classId) const
    {
        for (ClassProgress const& cp : _pool)
            if (cp.classId == classId)
                return &cp;
        return nullptr;
    }

    [[nodiscard]] ClassProgress* FindOwned(uint8 classId)
    {
        for (ClassProgress& cp : _pool)
            if (cp.classId == classId)
                return &cp;
        return nullptr;
    }

    std::vector<ClassProgress> _pool;    // every owned class (active or benched)
    std::vector<uint8> _slots;           // positional: _slots[i] = class in slot i, 0 = empty; holes allowed
    std::vector<uint8> _active;          // derived cache: non-empty classIds in slot order; front() = projection
    uint8 _maxActive = 3;                // active-set cap; injected from per-world config (P1b), 3 = fallback only
};

#endif // ACORE_MULTICLASS_PROFILE_H
