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

#ifndef MOD_MULTICLASS_SPELLS_H
#define MOD_MULTICLASS_SPELLS_H

#include "Define.h"
#include <cstdint>
#include <vector>

class Player;

namespace Multiclass
{
    // The class's character-creation spells for this race+class as if freshly created as `classId`:
    // the class-specific spells its default skill lines reward at the starting skill value, plus any
    // customSpells overlay. Empty if the race+class combo has no PlayerInfo (a vanilla-invalid pair).
    // A skill line's general (non-class) spells are handled by GrantClassSkills, not returned here.
    std::vector<uint32> StartingSpellsFor(Player* player, uint8 classId);

    // Grant `classId`'s default skill lines onto the player (idempotent) so the client recognises the
    // class's abilities — including at trainers, which the 3.3.5 client groups by skill line. Must be
    // called under the engine's per-player orchestration guard: SetSkill auto-learns a skill line's general spells.
    void GrantClassSkills(Player* player, uint8 classId);

    // OR of every SkillLineAbility ClassMask entry for the spell; 0 if it has no
    // class-specific entry (general/profession/racial). Used for attribution.
    uint32 CombinedClassMask(uint32 spellId);

    // True if spellId is a talent spell (mirrors Player::learnSpell's thisSpec
    // expression). Talent spells are out of scope for the spell ledger until the
    // talent plan; ledgering them would desync spellbook vs talent map on swap-out.
    bool IsTalentSpell(uint32 spellId);

    // The class that OWNS a talent spell, derived from its TalentTab.ClassMask (sTalentTabStore) --
    // projection-agnostic, never the projected/render class. 0 if spellId is not a talent or the tab
    // has no class. This is the per-class talent model's keystone lookup.
    uint8 TalentOwnerClass(uint32 spellId);
}

#endif
