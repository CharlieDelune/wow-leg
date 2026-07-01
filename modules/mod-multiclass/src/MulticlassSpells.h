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

namespace Multiclass
{
    // The class's character-creation spells (customSpells) for this race+class,
    // or empty if the race+class combo has no PlayerInfo (e.g. a vanilla-invalid pairing).
    std::vector<uint32> StartingSpellsFor(uint8 race, uint8 classId);

    // OR of every SkillLineAbility ClassMask entry for the spell; 0 if it has no
    // class-specific entry (general/profession/racial). Used for attribution.
    uint32 CombinedClassMask(uint32 spellId);
}

#endif
