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
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Trainer.h"
#include "Creature.h"
#include "NPCPackets.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace Trainer
{
    bool Spell::IsCastable() const
    {
        return sSpellMgr->AssertSpellInfo(SpellId)->HasEffect(SPELL_EFFECT_LEARN_SPELL);
    }

    Trainer::Trainer(uint32 trainerId, Type type, uint32 requirement, std::string greeting, std::vector<Spell> spells) : _trainerId(trainerId), _type(type), _requirement(requirement), _spells(std::move(spells))
    {
        _greeting[DEFAULT_LOCALE] = std::move(greeting);
    }

    void Trainer::SendSpells(Creature* npc, Player* player, LocaleConstant locale) const
    {
        float reputationDiscount = player->GetReputationPriceDiscount(npc);

        WorldPackets::NPC::TrainerList trainerList;
        trainerList.TrainerGUID = npc->GetGUID();
        trainerList.TrainerType = AsUnderlyingType(_type);
        trainerList.Greeting = GetGreeting(locale);
        trainerList.Spells.reserve(_spells.size());
        for (Spell const& trainerSpell : _spells)
        {
            if (!player->IsSpellTrainable(trainerSpell.SpellId))
                continue;

            SpellInfo const* trainerSpellInfo = sSpellMgr->AssertSpellInfo(trainerSpell.SpellId);

            bool primaryProfessionFirstRank = false;
            for (SpellEffectInfo const& spellEffectInfo : trainerSpellInfo->GetEffects())
            {
                if (!spellEffectInfo.IsEffect(SPELL_EFFECT_LEARN_SPELL))
                    continue;

                SpellInfo const* learnedSpellInfo = sSpellMgr->GetSpellInfo(spellEffectInfo.TriggerSpell);
                if (learnedSpellInfo && learnedSpellInfo->IsPrimaryProfessionFirstRank())
                    primaryProfessionFirstRank = true;
            }

            trainerList.Spells.emplace_back();
            WorldPackets::NPC::TrainerListSpell& trainerListSpell = trainerList.Spells.back();
            trainerListSpell.SpellID = trainerSpell.SpellId;
            SpellState const state = GetSpellState(player, &trainerSpell);
            trainerListSpell.Usable = AsUnderlyingType(state);
            trainerListSpell.MoneyCost = int32(trainerSpell.MoneyCost * reputationDiscount);
            trainerListSpell.PointCost[0] = 0; // spells don't cost talent points
            trainerListSpell.PointCost[1] = (primaryProfessionFirstRank ? 1 : 0);
            trainerListSpell.ReqLevel = trainerSpell.ReqLevel;
            // The 3.3.5 client independently greys/hides a trainer entry whose ReqLevel exceeds the
            // player's DISPLAYED level, regardless of the Usable state the server sends. A multiclass
            // character's owning-class level (already validated server-side in GetSpellState) can
            // exceed the displayed (lowest-active) level, so for a spell we will actually teach,
            // advertise a ReqLevel the client will accept — otherwise it hides a server-trainable
            // spell. No-op for single-class characters: an Available spell there always has
            // ReqLevel <= level, so the clamp never triggers.
            if (state == SpellState::Available && trainerListSpell.ReqLevel > player->GetLevel())
                trainerListSpell.ReqLevel = player->GetLevel();
            trainerListSpell.ReqSkillLine = trainerSpell.ReqSkillLine;
            trainerListSpell.ReqSkillRank = trainerSpell.ReqSkillRank;
            std::copy(trainerSpell.ReqAbility.begin(), trainerSpell.ReqAbility.end(), trainerListSpell.ReqAbility.begin());
        }

        player->SendDirectMessage(trainerList.Write());
    }

    void Trainer::TeachSpell(Creature* npc, Player* player, uint32 spellId)
    {
        if (!IsTrainerValidForPlayer(player))
            return;

        Spell const* trainerSpell = GetSpell(spellId);
        if (!trainerSpell)
        {
            SendTeachFailure(npc, player, spellId, FailReason::Unavailable);
            return;
        }

        if (!CanTeachSpell(player, trainerSpell))
        {
            SendTeachFailure(npc, player, spellId, FailReason::NotEnoughSkill);
            return;
        }

        float reputationDiscount = player->GetReputationPriceDiscount(npc);
        int32 moneyCost = int32(trainerSpell->MoneyCost * reputationDiscount);
        if (!player->HasEnoughMoney(moneyCost))
        {
            SendTeachFailure(npc, player, spellId, FailReason::NotEnoughMoney);
            return;
        }

        player->ModifyMoney(-moneyCost);

        npc->SendPlaySpellVisual(179); // 53 SpellCastDirected
        npc->SendPlaySpellImpact(player->GetGUID(), 362); // 113 EmoteSalute

        // learn explicitly or cast explicitly
        if (trainerSpell->IsCastable())
            player->CastSpell(player, trainerSpell->SpellId, true);
        else
            player->learnSpell(trainerSpell->SpellId, false);

        SendTeachSucceeded(npc, player, spellId);
    }

    Spell const* Trainer::GetSpell(uint32 spellId) const
    {
        auto itr = std::find_if(_spells.begin(), _spells.end(), [spellId](Spell const& trainerSpell)
        {
            return trainerSpell.SpellId == spellId;
        });

        if (itr != _spells.end())
            return &(*itr);

        return nullptr;
    }

    bool Trainer::CanTeachSpell(Player const* player, Spell const* trainerSpell) const
    {
        SpellState state = GetSpellState(player, trainerSpell);
        if (state != SpellState::Available)
            return false;

        SpellInfo const* trainerSpellInfo = sSpellMgr->AssertSpellInfo(trainerSpell->SpellId);

        for (SpellEffectInfo const& spellEffectInfo : trainerSpellInfo->GetEffects())
        {
            if (!spellEffectInfo.IsEffect(SPELL_EFFECT_LEARN_SPELL))
                continue;

            SpellInfo const* learnedSpellInfo = sSpellMgr->GetSpellInfo(spellEffectInfo.TriggerSpell);
            if (learnedSpellInfo && learnedSpellInfo->IsPrimaryProfessionFirstRank() && !player->GetFreePrimaryProfessionPoints())
                return false;
        }

        return true;
    }

    SpellState Trainer::GetSpellState(Player const* player, Spell const* trainerSpell) const
    {
        if (player->HasSpell(trainerSpell->SpellId))
            return SpellState::Known;

        // check race/class requirement — multiclass: an unlocked (possibly benched) off-class may
        // train its own spells, so gate on the unlocked class mask rather than the active class.
        if (!player->IsSpellTrainable(trainerSpell->SpellId))
            return SpellState::Unavailable;

        // check skill requirement
        if (trainerSpell->ReqSkillLine && player->GetBaseSkillValue(trainerSpell->ReqSkillLine) < trainerSpell->ReqSkillRank)
            return SpellState::Unavailable;

        for (int32 reqAbility : trainerSpell->ReqAbility)
            if (reqAbility && !player->HasSpell(reqAbility))
                return SpellState::Unavailable;

        // check level requirement — multiclass: gate on the player's level in the (possibly benched)
        // unlocked class that owns this spell, not the displayed (lowest active) level. Falls back to
        // GetLevel() for spells no class owns and for single-class characters.
        if (player->GetUnlockedClassLevelForSpell(trainerSpell->SpellId) < trainerSpell->ReqLevel)
            return SpellState::Unavailable;

        // check ranks
        bool hasLearnSpellEffect = false;
        bool knowsAllLearnedSpells = true;
        for (SpellEffectInfo const& spellEffectInfo : sSpellMgr->AssertSpellInfo(trainerSpell->SpellId)->GetEffects())
        {
            if (!spellEffectInfo.IsEffect(SPELL_EFFECT_LEARN_SPELL))
                continue;

            hasLearnSpellEffect = true;
            if (!player->HasSpell(spellEffectInfo.TriggerSpell))
                knowsAllLearnedSpells = false;

            if (uint32 previousRankSpellId = sSpellMgr->GetPrevSpellInChain(spellEffectInfo.TriggerSpell))
                if (!player->HasSpell(previousRankSpellId))
                    return SpellState::Unavailable;
        }

        if (!hasLearnSpellEffect)
        {
            if (uint32 previousRankSpellId = sSpellMgr->GetPrevSpellInChain(trainerSpell->SpellId))
                if (!player->HasSpell(previousRankSpellId))
                    return SpellState::Unavailable;
        }
        else if (knowsAllLearnedSpells)
            return SpellState::Known;

        // check additional spell requirement
        for (auto const& requirePair : sSpellMgr->GetSpellsRequiredForSpellBounds(trainerSpell->SpellId))
            if (!player->HasSpell(requirePair.second))
                return SpellState::Unavailable;

        return SpellState::Available;
    }

    bool Trainer::IsTrainerValidForPlayer(Player const* player) const
    {
        if (!GetTrainerRequirement())
            return true;

        switch (GetTrainerType())
        {
            case Type::Class:
            case Type::Pet:
                // check class for class trainers; multiclass: honor the unlocked class mask so an
                // unlocked (possibly benched) off-class can reach and use its own trainer (both the
                // gossip option and the spell list). This function is trainer-only, so widening to the
                // unlocked mask here never affects non-trainer class gates.
                return (player->GetUnlockedClassMask() & (1 << (GetTrainerRequirement() - 1))) != 0;
            case Type::Mount:
                // check race for mount trainers
                return player->getRace() == GetTrainerRequirement();
            case Type::Tradeskill:
                // check spell for profession trainers
                return player->HasSpell(GetTrainerRequirement());
            default:
                break;
        }

        return true;
    }

    void Trainer::SendTeachFailure(Creature const* npc, Player const* player, uint32 spellId, FailReason reason) const
    {
        WorldPackets::NPC::TrainerBuyFailed trainerBuyFailed;
        trainerBuyFailed.TrainerGUID = npc->GetGUID();
        trainerBuyFailed.SpellID = spellId;
        trainerBuyFailed.TrainerFailedReason = AsUnderlyingType(reason);
        player->SendDirectMessage(trainerBuyFailed.Write());
    }

    void Trainer::SendTeachSucceeded(Creature const* npc, Player const* player, uint32 spellId) const
    {
        WorldPackets::NPC::TrainerBuySucceeded trainerBuySucceeded;
        trainerBuySucceeded.TrainerGUID = npc->GetGUID();
        trainerBuySucceeded.SpellID = spellId;
        player->SendDirectMessage(trainerBuySucceeded.Write());
    }

    std::string const& Trainer::GetGreeting(LocaleConstant locale) const
    {
        if (_greeting[locale].empty())
            return _greeting[DEFAULT_LOCALE];

        return _greeting[locale];
    }

    void Trainer::AddGreetingLocale(LocaleConstant locale, std::string greeting)
    {
        _greeting[locale] = std::move(greeting);
    }
}
