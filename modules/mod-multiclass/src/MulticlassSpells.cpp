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

#include "MulticlassSpells.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellMgr.h"

namespace Multiclass
{
    std::vector<uint32> StartingSpellsFor(uint8 race, uint8 classId)
    {
        std::vector<uint32> spells;
        PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race, classId);
        if (!info)
            return spells;

        for (uint32 spellId : info->customSpells)
            spells.push_back(spellId);
        return spells;
    }

    uint32 CombinedClassMask(uint32 spellId)
    {
        uint32 mask = 0;
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
            mask |= itr->second->ClassMask;
        return mask;
    }
}
