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
#include "DBCEnums.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellMgr.h"
#include "World.h"

namespace Multiclass
{
    std::vector<uint32> StartingSpellsFor(Player* player, uint8 classId)
    {
        std::vector<uint32> spells;
        PlayerInfo const* info = sObjectMgr->GetPlayerInfo(player->getRace(), classId);
        if (!info)
            return spells;

        uint32 const raceMask = player->getRaceMask();
        uint32 const classMask = 1u << (classId - 1);
        uint16 const skillCap = player->GetMaxSkillValueForLevel();

        // Mirror Player::learnSkillRewardedSpells across the class's default skill lines, but filtered
        // to `classId` (an off-class) rather than the render class: collect the class-specific spells
        // those skills reward at the starting skill value. Only class-masked spells are returned (they
        // become the per-class ledger and are removed on bench); a skill line's general spells are left
        // to SetSkill's own auto-learn in GrantClassSkills, so they persist like a normal character's.
        for (PlayerCreateInfoSkill const& skill : info->skills)
        {
            for (SkillLineAbilityEntry const* ability : GetSkillLineAbilitiesBySkillLine(skill.SkillId))
            {
                if (ability->AcquireMethod != SKILL_LINE_ABILITY_LEARNED_ON_SKILL_VALUE &&
                    ability->AcquireMethod != SKILL_LINE_ABILITY_LEARNED_ON_SKILL_LEARN)
                    continue;
                if (ability->RaceMask && !(ability->RaceMask & raceMask))
                    continue;
                if (!ability->ClassMask || !(ability->ClassMask & classMask))
                    continue;
                // Don't over-grant higher ranks at creation: value-gated abilities above the starting
                // skill value are learned later (at trainers), exactly as for a freshly created char.
                if (ability->AcquireMethod == SKILL_LINE_ABILITY_LEARNED_ON_SKILL_VALUE &&
                    ability->MinSkillLineRank > skillCap)
                    continue;
                // Skip skill-line entries whose spell isn't in the server's spell store (dead DBC rows,
                // e.g. rogue's 75460): the core rejects them at learn time anyway, and banking them into
                // the ledger only yields junk rows and repeated "Non-existed in SpellStore" retries.
                if (!sSpellMgr->GetSpellInfo(ability->Spell))
                    continue;
                spells.push_back(ability->Spell);
            }
        }

        // Custom creation-spell overlay (playercreateinfo_spell_custom) -- gated on the same config the
        // core's Player::LearnCustomSpells honours (CONFIG_START_CUSTOM_SPELLS / PlayerStart.CustomSpells).
        // A freshly created character only receives these when the config is on, and the unlock kit must
        // match a fresh character; applying them unconditionally hands the off-class spells (and high
        // ranks) no real character of that class would get -- e.g. ARAC's populated table -> 72 mage spells.
        if (sWorld->getBoolConfig(CONFIG_START_CUSTOM_SPELLS))
            for (uint32 spellId : info->customSpells)
                spells.push_back(spellId);

        return spells;
    }

    void GrantClassSkills(Player* player, uint8 classId)
    {
        PlayerInfo const* info = sObjectMgr->GetPlayerInfo(player->getRace(), classId);
        if (!info)
            return;

        // Grant the class's default skill lines (mirrors Player::LearnDefaultSkills, parameterised to
        // `classId`). GetSkillRaceClassInfo validates the (skill, race, class) triple; skills already
        // present (shared weapon skills, languages, the render class's own lines) are left untouched.
        // SetSkill auto-learns each line's general spells — hence the required orchestration guard.
        uint16 const skillCap = player->GetMaxSkillValueForLevel();
        for (PlayerCreateInfoSkill const& skill : info->skills)
        {
            if (player->HasSkill(skill.SkillId))
                continue;
            if (!GetSkillRaceClassInfo(skill.SkillId, player->getRace(), classId))
                continue;
            player->SetSkill(skill.SkillId, skill.Rank, 1, skillCap);
        }
    }

    uint32 CombinedClassMask(uint32 spellId)
    {
        uint32 mask = 0;
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
            mask |= itr->second->ClassMask;
        return mask;
    }

    bool IsTalentSpell(uint32 spellId)
    {
        // Mirror Player::learnSpell's thisSpec expression exactly.
        // Talent spells are managed by the core talent map; ledgering them would
        // desync spellbook vs talent map on swap-out. Out of scope until the talent plan.
        uint32 const first = sSpellMgr->GetFirstSpellInChain(spellId);
        return GetTalentSpellCost(first) > 0 || sSpellMgr->IsAdditionalTalentSpell(first);
    }

    uint8 TalentOwnerClass(uint32 spellId)
    {
        TalentSpellPos const* pos = GetTalentSpellPos(spellId);
        if (!pos)
            return 0;
        TalentEntry const* talentInfo = sTalentStore.LookupEntry(pos->talent_id);
        if (!talentInfo)
            return 0;
        TalentTabEntry const* tabInfo = sTalentTabStore.LookupEntry(talentInfo->TalentTab);
        return tabInfo ? ClassIdFromMask(tabInfo->ClassMask) : uint8(0);
    }
}
