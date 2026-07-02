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

    // ---- active set ----
    [[nodiscard]] std::vector<uint8> const& GetActiveClasses() const { return _active; }

    [[nodiscard]] uint8 GetProjectedClass() const { return _active.empty() ? uint8(0) : _active.front(); }

    [[nodiscard]] bool HasActiveClass(uint8 classId) const
    {
        return std::find(_active.begin(), _active.end(), classId) != _active.end();
    }

    [[nodiscard]] uint32 GetActiveClassMask() const
    {
        uint32 mask = 0;
        for (uint8 classId : _active)
            if (classId != 0)
                mask |= 1u << (classId - 1);
        return mask;
    }

    bool Activate(uint8 classId)
    {
        if (!HasOwnedClass(classId) || HasActiveClass(classId) || _active.size() >= _maxActive)
            return false;
        _active.push_back(classId);
        return true;
    }

    bool Deactivate(uint8 classId)
    {
        if (_active.size() <= 1)
            return false;
        auto itr = std::find(_active.begin(), _active.end(), classId);
        if (itr == _active.end())
            return false;
        _active.erase(itr);
        return true;
    }

    // Replace the class occupying active position `index` with `newClassId` (which must already be
    // owned). Atomic — the active set is never transiently emptied, so this is the safe way to swap
    // the last active class. The displaced class remains owned (benched). Rejects out-of-range index,
    // invalid/unowned newClassId, and a newClassId already active at a different position.
    bool ReplaceActiveAt(uint8 index, uint8 newClassId)
    {
        if (index >= _active.size())
            return false;
        if (!IsValidClassId(newClassId) || !HasOwnedClass(newClassId))
            return false;
        for (std::size_t i = 0; i < _active.size(); ++i)
            if (i != index && _active[i] == newClassId)
                return false;
        _active[index] = newClassId;
        return true;
    }

    // Reset and repopulate from persisted data. `pool` is every owned class; `activeOrder` is the
    // active classIds in slot order (each must appear in `pool`). Honors the current cap.
    void Load(std::vector<ClassProgress> const& pool, std::vector<uint8> const& activeOrder)
    {
        _pool.clear();
        _active.clear();
        for (ClassProgress const& cp : pool)
            AddOwnedClass(cp.classId, cp.level, cp.xp);
        for (uint8 classId : activeOrder)
            Activate(classId);
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
    std::vector<uint8> _active;          // ordered active classIds; front() is the projection
    uint8 _maxActive = 3;                // active-set cap; injected from per-world config (P1b), 3 = fallback only
};

#endif // ACORE_MULTICLASS_PROFILE_H
