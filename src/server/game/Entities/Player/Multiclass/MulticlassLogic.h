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

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Define.h"

// Governs how per-active-class stat values are reduced into the single value a multiclass character
// carries (bound to Multiclass.CombinedStats -> CONFIG_MULTICLASS_COMBINED_STATS). File scope so
// WorldConfig.cpp, Player.h and StatSystem.cpp all see it without pulling in Player.
enum MulticlassCombinedStats : uint32
{
    MULTICLASS_STATS_HIGHEST = 0,   // per-stat max across active classes (bounded, no-inflation default)
    MULTICLASS_STATS_SUM     = 1    // additive across active classes (the OP sandbox)
};

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

    // The classId (1..11) encoded by a class mask, or 0 if none. Talent tabs and SkillLineAbility rows
    // carry a single-class mask; this decodes it without a DBC read (pure -> unit-testable). Multi-bit
    // masks resolve to the lowest set classId, which suffices for the single-bit talent-tab case.
    inline uint8 ClassIdFromMask(uint32 classMask)
    {
        if (classMask == 0)
            return 0;
        for (uint8 classId = 1; classId <= 11; ++classId)
            if (classMask & (1u << (classId - 1)))
                return classId;
        return 0;
    }

    inline uint8 ActiveCount(SlotArray const& slots)
    {
        uint8 count = 0;
        for (ClassProgress const& slot : slots)
            if (slot.classId != 0)
                ++count;
        return count;
    }

    // Combined class mask of the active (classId != 0) slots: OR of 1 << (classId - 1).
    // The union with the render class is applied by the caller (the hook ORs this into
    // getClassMask()), so trainer / spell-fit gating never regresses the render class.
    inline uint32 ActiveClassMask(SlotArray const& slots)
    {
        uint32 mask = 0;
        for (ClassProgress const& slot : slots)
            if (slot.classId != 0)
                mask |= 1u << (slot.classId - 1);
        return mask;
    }

    // Level of the active slot holding classId, or 0 if no active slot holds it.
    inline uint8 ClassLevel(SlotArray const& slots, uint8 classId)
    {
        if (classId == 0)
            return 0;
        for (ClassProgress const& slot : slots)
            if (slot.classId == classId)
                return slot.level;
        return 0;
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

    // Glyph slot -> the character level at which that slot unlocks (retail: slots 0/1 at 15, slot 3 at 30,
    // slot 2 at 50, slot 4 at 70, slot 5 at 80). Pure so both the socket-time gate and the enabled-slot
    // bitmask derive from ONE source. Out-of-range slots return 0 (no gate).
    inline uint8 GlyphSlotUnlockLevel(uint8 slot)
    {
        switch (slot)
        {
            case 0:
            case 1:
                return 15;
            case 2:
                return 50;
            case 3:
                return 30;
            case 4:
                return 70;
            case 5:
                return 80;
            default:
                return 0;
        }
    }

    // The PLAYER_GLYPHS_ENABLED bitmask for a level: bit i set when level >= GlyphSlotUnlockLevel(i).
    // Byte-identical to the native InitGlyphsForLevel ladder (0x03@15, +0x08@30, +0x04@50, +0x10@70,
    // +0x20@80). `slotCount` is passed in (== MAX_GLYPH_SLOT_INDEX) so this header needs no game include.
    inline uint32 GlyphEnabledSlotMask(uint8 level, uint8 slotCount)
    {
        uint32 mask = 0;
        for (uint8 slot = 0; slot < slotCount; ++slot)
        {
            uint8 const unlock = GlyphSlotUnlockLevel(slot);
            if (unlock && level >= unlock)
                mask |= (1u << slot);
        }
        return mask;
    }

    // Native talent-reset cost ladder, extracted pure for unit testing and reuse by the per-class ladder.
    // `lastCostCopper` is the last cost paid (0 == never reset), `secondsSinceReset` is (now - lastReset),
    // `decayPeriodSeconds` is the tunable decay window (native == 30 days). Mirrors retail exactly: opening
    // tiers 1g -> 5g -> 10g, then +5g per reset up to a 50g cap, decaying 5g per full decay-period elapsed
    // down to a 10g floor. At decayPeriodSeconds == 30*DAY this is byte-identical to native resetTalentsCost.
    inline uint32 TalentResetCost(uint32 lastCostCopper, uint32 secondsSinceReset, uint32 decayPeriodSeconds)
    {
        constexpr uint32 GOLD_COPPER = 10000;   // == SharedDefines.h GOLD (SILVER*100); kept local so this
                                                // pure header needs no game include and stays unit-testable.
        if (lastCostCopper < 1 * GOLD_COPPER)
            return 1 * GOLD_COPPER;
        if (lastCostCopper < 5 * GOLD_COPPER)
            return 5 * GOLD_COPPER;
        if (lastCostCopper < 10 * GOLD_COPPER)
            return 10 * GOLD_COPPER;

        uint32 const periods = decayPeriodSeconds ? (secondsSinceReset / decayPeriodSeconds) : 0u;
        if (periods > 0)
        {
            int32 const decayed = int32(lastCostCopper) - int32(5 * GOLD_COPPER) * int32(periods);
            return decayed < int32(10 * GOLD_COPPER) ? 10 * GOLD_COPPER : uint32(decayed);
        }

        uint32 climbed = lastCostCopper + 5 * GOLD_COPPER;
        if (climbed > 50 * GOLD_COPPER)
            climbed = 50 * GOLD_COPPER;
        return climbed;
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

    // OR of 1 << (classId - 1) over every unlocked class (the full pool, active + benched).
    // The render class is unioned by the caller's getClassMask() seam, exactly like ActiveClassMask.
    inline uint32 UnlockedClassMask(std::unordered_map<uint8, ClassProgress> const& pool)
    {
        uint32 mask = 0;
        for (auto const& entry : pool)
            if (entry.first != 0)
                mask |= 1u << (entry.first - 1);
        return mask;
    }

    // Remembered level of an unlocked class, or 0 if the class is not unlocked.
    inline uint8 UnlockedClassLevel(std::unordered_map<uint8, ClassProgress> const& pool, uint8 classId)
    {
        auto itr = pool.find(classId);
        return itr != pool.end() ? itr->second.level : uint8(0);
    }

    // Which unlocked classes (active or benched) own a spell, given the OR of its SkillLineAbility
    // class masks (0 == no class-specific entry => owned by none). Mirrors ClaimingClasses but ranges
    // over the full pool; returned ascending by classId for determinism.
    inline std::vector<uint8> ClaimingUnlockedClasses(
        std::unordered_map<uint8, ClassProgress> const& pool, uint32 combinedClassMask)
    {
        std::vector<uint8> result;
        if (combinedClassMask == 0)
            return result;
        for (auto const& entry : pool)
            if (entry.first != 0 && (combinedClassMask & (1u << (entry.first - 1))))
                result.push_back(entry.first);
        std::sort(result.begin(), result.end());
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

    // Reduce per-active-class stat values into the single coherent value the character carries, per
    // Multiclass.CombinedStats: MULTICLASS_STATS_SUM -> additive, otherwise (HIGHEST, the default) ->
    // std::max. Empty input yields the neutral 0.0f; a single element returns that element unchanged
    // under BOTH modes -- the byte-vanilla single-class invariant lives here.
    inline float CombineValues(uint32 mode, std::vector<float> const& perClassValues)
    {
        if (perClassValues.empty())
            return 0.0f;

        if (mode == MULTICLASS_STATS_SUM)
        {
            float sum = 0.0f;
            for (float value : perClassValues)
                sum += value;
            return sum;
        }

        float best = perClassValues.front();
        for (float value : perClassValues)
            best = std::max(best, value);
        return best;
    }

    // Replaces the WoW class token (normally client-expanded) in narrative text: "$C" -> forUpper, "$c" ->
    // forLower. Only the class token is touched; $R/$N/$G and a bare/escaped "$$" are left intact. No-op if
    // `text` has no '$'. Callers supply either the class-agnostic word (see DeclassifyText) or a client-side
    // marker (see MarkClassToken) as the replacements.
    inline void ReplaceClassToken(std::string& text, std::string_view forUpper, std::string_view forLower)
    {
        if (text.find('$') == std::string::npos)
            return;

        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '$' && i + 1 < text.size())
            {
                // "$$" is an escaped literal '$': copy both bytes so the second '$' cannot start a token
                // (e.g. "$$C" stays "$$C" rather than expanding the trailing "$C").
                if (text[i + 1] == '$')
                {
                    out.push_back('$');
                    out.push_back('$');
                    ++i;
                    continue;
                }
                if (text[i + 1] == 'C')
                {
                    out.append(forUpper.data(), forUpper.size());
                    ++i;
                    continue;
                }
                if (text[i + 1] == 'c')
                {
                    out.append(forLower.data(), forLower.size());
                    ++i;
                    continue;
                }
            }
            out.push_back(text[i]);
        }
        text.swap(out);
    }

    // Replaces the class token with a class-agnostic word, so a multiclass character's world-facing text never
    // names a single class. "$C" -> word with its first ASCII byte upper-cased (WoW sentence-position
    // convention); "$c" -> word verbatim.
    inline void DeclassifyText(std::string& text, std::string_view word)
    {
        if (word.empty())
            return;

        std::string upper(word);
        upper[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(upper[0])));
        ReplaceClassToken(text, upper, word);
    }

    // ---- Talent-removal cascade (Loadouts Phase 0) ------------------------------------------------
    // native PLAYER_TALENTS_PER_TIER; tier gate = tier * this (kept here so this header stays DBC-free)
    inline constexpr uint32 TalentPointsPerTier = 5;

    struct TalentRecord
    {
        uint32 talentId;   // TalentEntry.TalentID
        uint32 tab;        // TalentEntry.TalentTab
        uint32 tier;       // TalentEntry.Row (0-based)
        uint32 dep;        // TalentEntry.DependsOn (0 = none)
        uint32 depRank;    // TalentEntry.DependsOnRank (0-based)
        uint32 rank;       // current point count 1..5
    };

    // Remove `count` points from `targetId`, then cascade-invalidate any still-learned talent whose tier
    // gate (total points in its tab >= tier * TalentPointsPerTier) or prerequisite (dep held at rank >=
    // depRank + 1) is no longer met, iterating to a fixpoint. The tier gate mirrors SpendClassTalent's
    // (total tab points), so any surviving config is one the spend path also accepts. Returns the
    // resulting point count for every input talent (0 == removed), in input order.
    inline std::vector<std::pair<uint32, uint32>> ComputeTalentRemovalCascade(
        std::vector<TalentRecord> const& learned, uint32 targetId, uint32 count)
    {
        std::unordered_map<uint32, uint32> rank;
        std::unordered_map<uint32, TalentRecord const*> byId;
        for (auto const& r : learned)
        {
            rank[r.talentId] = r.rank;
            byId[r.talentId] = &r;
        }

        if (auto it = rank.find(targetId); it != rank.end())
            it->second = count >= it->second ? 0u : it->second - count;

        for (bool changed = true; changed; )
        {
            changed = false;
            std::unordered_map<uint32, uint32> tabPoints;
            for (auto const& kv : rank)
                if (kv.second)
                    tabPoints[byId[kv.first]->tab] += kv.second;

            for (auto& kv : rank)
            {
                if (!kv.second)
                    continue;
                TalentRecord const& rec = *byId[kv.first];
                bool invalid = rec.tier && tabPoints[rec.tab] < rec.tier * TalentPointsPerTier;
                if (!invalid && rec.dep)
                {
                    auto d = rank.find(rec.dep);
                    invalid = (d == rank.end() ? 0u : d->second) < rec.depRank + 1;
                }
                if (invalid)
                {
                    kv.second = 0;
                    changed = true;
                }
            }
        }

        std::vector<std::pair<uint32, uint32>> out;
        out.reserve(learned.size());
        for (auto const& r : learned)
            out.emplace_back(r.talentId, rank[r.talentId]);
        return out;
    }

    // ---- Loadout list management (Loadouts Phase 1) ----------------------------------------------
    struct LoadoutMeta
    {
        uint32 id;
        std::string name;
        std::string description;   // free-text note (Phase 1 metadata)
        std::string icon;          // macro-picker texture string; drives the Phase 3 hotbar switch macro
        uint32 sortOrder;
    };

    // Never reuse ids (stable references); one past the current max.
    inline uint32 NextLoadoutId(std::vector<LoadoutMeta> const& existing)
    {
        uint32 maxId = 0;
        for (auto const& l : existing)
            maxId = l.id > maxId ? l.id : maxId;
        return maxId + 1;
    }

    inline bool CanCreateLoadout(std::size_t count, uint32 capacity)
    {
        return count < capacity;
    }

    // Copper cost of the next purchasable loadout slot: base + alreadyPurchased * step, saturating so an
    // absurd ratchet can never wrap. Pure so the escalation curve is unit-tested independent of config.
    [[nodiscard]] inline uint32 LoadoutSlotCostCopper(uint32 alreadyPurchased, uint32 baseCopper, uint32 stepCopper)
    {
        uint64_t const cost = uint64_t(baseCopper) + uint64_t(alreadyPurchased) * uint64_t(stepCopper);
        return cost > uint64_t(UINT32_MAX) ? UINT32_MAX : uint32(cost);
    }

    struct DeleteResolution
    {
        bool allowed;
        uint32 newActiveId;   // meaningful only when allowed; == activeId if the active one survives
    };

    // Reject deleting the last loadout or an unknown id. Deleting the active one picks the lowest
    // surviving id to become active; deleting an inactive one leaves active unchanged.
    inline DeleteResolution ResolveDeleteTarget(std::vector<LoadoutMeta> const& loadouts, uint32 deleteId, uint32 activeId)
    {
        if (loadouts.size() <= 1)
            return { false, activeId };
        bool found = false;
        uint32 fallback = 0;
        for (auto const& l : loadouts)
        {
            if (l.id == deleteId)
                found = true;
            else if (fallback == 0 || l.id < fallback)
                fallback = l.id;
        }
        if (!found)
            return { false, activeId };
        return { true, deleteId == activeId ? fallback : activeId };
    }
}

#endif
