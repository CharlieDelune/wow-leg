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
        uint32 talentResetCost = 0;   // last talent-reset cost paid for this class (copper); 0 == never reset
        uint32 talentResetTime = 0;   // unixtime of this class's last talent reset; 0 == never reset
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

    bool AddOwnedClass(uint8 classId, uint8 level = 1, uint32 xp = 0, uint32 talentResetCost = 0,
        uint32 talentResetTime = 0)
    {
        if (!IsValidClassId(classId) || HasOwnedClass(classId))
            return false;
        _pool.push_back(ClassProgress{ classId, level, xp, talentResetCost, talentResetTime });
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

    // Replace the entire active set with `order` — the new compact, slot-order list (slot 0 first, no
    // holes). Unlike SetSlot/ClearSlot (single-slot deltas) this is a whole-set rewrite, so the class
    // panel can apply an arbitrary add/remove/reorder in one shot without transient duplicate-slot
    // conflicts. The caller (ValidateSetOrderRequest / MulticlassEngine::SetActiveOrder) has already
    // checked ownership, cap, duplicates, and non-empty; every entry is a valid owned non-zero classId.
    void SetActiveOrder(std::vector<uint8> const& order)
    {
        _slots = order;
        RebuildActiveCache();
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
            AddOwnedClass(cp.classId, cp.level, cp.xp, cp.talentResetCost, cp.talentResetTime);
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

        // Rescue a lone stranded class: if the cap trimmed away every slotted position (all in-range slots
        // were holes) but a class was persisted at a slot >= _maxActive, a managed character would load with
        // no active class. Activate the lowest such owned class into slot 0 so the projection always has one.
        if (_active.empty())
        {
            for (uint8 classId : positionalSlots)
            {
                if (classId != 0 && HasOwnedClass(classId))
                {
                    _slots.assign(1, classId);
                    RebuildActiveCache();
                    break;
                }
            }
        }
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

    [[nodiscard]] uint32 GetTalentResetCost(uint8 classId) const
    {
        ClassProgress const* cp = FindOwned(classId);
        return cp ? cp->talentResetCost : 0u;
    }

    [[nodiscard]] uint32 GetTalentResetTime(uint8 classId) const
    {
        ClassProgress const* cp = FindOwned(classId);
        return cp ? cp->talentResetTime : 0u;
    }

    // Set (or advance) a class's reset ladder. The loader passes the persisted pair; a completed reset
    // passes Multiclass::TalentResetCost(...) and the current time. Pure storage -> no config/time here,
    // keeping the profile unit-testable. False if the class is not owned.
    bool SetTalentResetLadder(uint8 classId, uint32 cost, uint32 time)
    {
        ClassProgress* cp = FindOwned(classId);
        if (!cp)
            return false;
        cp->talentResetCost = cost;
        cp->talentResetTime = time;
        return true;
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
    // The effective cap on simultaneously active classes is DERIVED from two inputs:
    //   _ceiling       -- the per-world Multiclass.MaxActiveClasses server limit (injected at load).
    //   _unlockedSlots -- this character's earned capacity (progression ratchet; persisted).
    // Effective cap = max(1, min(_unlockedSlots, _ceiling)), cached in _maxActive so every existing reader
    // (SetSlot, Load, GetMaxActiveClasses) is unchanged.
    static constexpr uint8 kSlotCapacityLevel2   = 5;    // 2nd active slot unlocks at this level
    static constexpr uint8 kSlotCapacityLevelAll = 10;   // all remaining slots unlock at this level
    static constexpr uint8 kSlotCapacityMax      = 11;   // absolute ceiling == MAX_CLASSES - 1

    [[nodiscard]] uint8 GetMaxActiveClasses() const { return _maxActive; }
    [[nodiscard]] uint8 GetUnlockedSlots() const { return _unlockedSlots; }
    [[nodiscard]] uint8 GetActiveCeiling() const { return _ceiling; }

    // Set the per-world ceiling input (floored at 1) and recompute the effective cap.
    void SetActiveCeiling(uint8 ceiling)
    {
        _ceiling = ceiling < 1 ? uint8(1) : ceiling;
        RecomputeMaxActive();
    }

    // Set this character's earned capacity (clamped to 1..kSlotCapacityMax) and recompute the effective cap.
    void SetUnlockedSlots(uint8 unlocked)
    {
        _unlockedSlots = unlocked < 1 ? uint8(1) : (unlocked > kSlotCapacityMax ? kSlotCapacityMax : unlocked);
        RecomputeMaxActive();
    }

    // Monotonic raise: bump earned capacity to `target` only if it exceeds the current value; never lowers.
    // Returns true iff the value changed, so callers persist only on an actual raise. The level rule and
    // future purchase/quest sources use this; the absolute GM override uses SetUnlockedSlots directly.
    bool RaiseUnlockedTo(uint8 target)
    {
        if (target <= _unlockedSlots)
            return false;
        SetUnlockedSlots(target);   // clamps to 1..kSlotCapacityMax and recomputes the effective cap
        return true;
    }

    // Relocate the sole remaining active class into slot 0 when a shrinking cap has stranded it above the
    // cap (all of [0, _maxActive) is empty but the one active class sits at an index >= _maxActive). It
    // cannot be benched -- a managed character must keep one active class -- so it is moved into range
    // instead. No-op (returns false) unless exactly one class is active AND it is out of range. Pure slot
    // move: the class stays active, only its position changes.
    bool CompactSoleActiveIntoCap()
    {
        if (_active.size() != 1)
            return false;
        std::size_t from = _slots.size();
        for (std::size_t i = 0; i < _slots.size(); ++i)
            if (_slots[i] != 0)
            {
                from = i;
                break;
            }
        if (from == _slots.size() || from < _maxActive)
            return false;   // no active class, or it is already within the cap
        uint8 const classId = _slots[from];
        _slots[from] = 0;
        _slots[0] = classId;   // slot 0 is guaranteed empty: this is the only filled slot and from >= 1
        RebuildActiveCache();
        return true;
    }

    // The highest slot index holding an active class at or above the cap -- an illegal position once the cap
    // shrank -- or -1 if every filled slot is within [0, _maxActive). This is the over-cap DETECTION that
    // drives eviction: POSITION-based (index >= cap), never filled-count-based. A positional hole can make
    // the filled count fit the cap while a class still sits out of range (Warrior@0, hole@1, Mage@2, cap 2:
    // count 2 == cap 2, yet Mage@2 is over-cap) -- the count-based check that shipped first missed exactly
    // that. Returned as int so -1 is unambiguous.
    [[nodiscard]] int HighestActiveSlotAboveCap() const
    {
        for (int i = int(_slots.size()) - 1; i >= int(_maxActive); --i)
            if (_slots[i] != 0)
                return i;
        return -1;
    }

    // Furthest progression across the whole owned pool (active or benched) -- the level-rule input.
    [[nodiscard]] uint8 GetMaxOwnedLevel() const
    {
        uint8 maxLevel = 0;
        for (ClassProgress const& cp : _pool)
            if (cp.level > maxLevel)
                maxLevel = cp.level;
        return maxLevel;
    }

    // The default level rule: active-slot capacity earned purely from character level (furthest
    // progression). Pure policy with no dependencies, so it lives here where it is unit-testable in
    // isolation; future purchase/quest sources bump capacity via the engine's monotonic GrantSlotCapacity.
    [[nodiscard]] static uint8 SlotCapacityForLevel(uint8 level)
    {
        if (level < kSlotCapacityLevel2)
            return 1;
        if (level < kSlotCapacityLevelAll)
            return 2;
        return kSlotCapacityMax;
    }

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

    // Recompute the cached effective cap from the two inputs. Both are floored at 1, so the result is >= 1.
    void RecomputeMaxActive()
    {
        uint8 const derived = _unlockedSlots < _ceiling ? _unlockedSlots : _ceiling;
        _maxActive = derived < 1 ? uint8(1) : derived;
    }

    std::vector<ClassProgress> _pool;    // every owned class (active or benched)
    std::vector<uint8> _slots;           // positional: _slots[i] = class in slot i, 0 = empty; holes allowed
    std::vector<uint8> _active;          // derived cache: non-empty classIds in slot order; front() = projection
    uint8 _unlockedSlots = kSlotCapacityMax;  // earned capacity (persisted ratchet); default = no per-char limit
    uint8 _ceiling       = 3;                 // per-world server limit; default matches the legacy fallback
    uint8 _maxActive     = 3;                 // derived cache = max(1, min(_unlockedSlots, _ceiling))
};

#endif // ACORE_MULTICLASS_PROFILE_H
