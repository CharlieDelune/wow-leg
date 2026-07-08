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

#ifndef MULTICLASS_ENGINE_H
#define MULTICLASS_ENGINE_H

#include "MulticlassLogic.h"
#include "ObjectGuid.h"
#include <array>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;
struct ItemTemplate;

namespace Multiclass
{
    // Per-character spell ledger: active classId -> learned class-specific spell IDs.
    // The class SET (owned pool + active + per-class level/xp) now lives in
    // Player::GetMulticlassProfile(); this ledger only tracks per-class spell banking
    // (character_multiclass_spell).
    using Ledger = std::unordered_map<uint8, std::unordered_set<uint32>>;

    void LoadLedger(Player* player);
    void SaveLedger(Player* player, bool sync = false);
    void UnlockClass(Player* player, uint8 classId);
    void ActivateClass(Player* player, uint8 slot, uint8 classId);
    void SwapSlotClass(Player* player, uint8 slot, uint8 newClassId);
    void UnsetSlot(Player* player, uint8 slot);
    void SetActiveOrder(Player* player, std::vector<uint8> const& order);   // whole-set rewrite (class panel)
    void GrantSlotCapacity(Player* player, uint8 target);   // monotonic: raise earned capacity, never lower
    void SetSlotCapacity(Player* player, uint8 n);          // absolute: set earned capacity, evict if lower
    void SetManagedLevel(Player* player, uint8 level);      // GM level command: set every active class to level
    void EnforceActiveCapacity(Player* player);             // bench over-cap slots (highest index first)
    void ReconcileDisplayLevel(Player* player);
    void RouteExperience(Player* player, uint32 effectiveXp);
    void AttributeLearnedSpell(Player* player, uint32 spellId);
    void AttributeForgotSpell(Player* player, uint32 spellId);
    void BackfillActiveLedgers(Player* player);
    void SendClientState(Player* player);
    void SendPeer(Player* recipient, std::string_view name, std::vector<uint8> const& active);

    // The glyph (GlyphProperties id) a glyph item teaches: scan the item's on-use spells for the one with a
    // SPELL_EFFECT_APPLY_GLYPH effect and return that effect's MiscValue. 0 if the item teaches no glyph.
    uint32 GlyphIdFromItem(ItemTemplate const* proto);

    // True when world-facing narrative text should be declassified for this player: multiclass enabled +
    // managed, 2+ active classes (a single active class keeps its real class name = vanilla), non-empty word.
    bool ShouldDeclassify(Player const* player);

    // Site-facing wrapper for LIVE (un-cached) surfaces: replace $C/$c in `text` with the configured diegetic
    // word iff ShouldDeclassify(player) — per-recipient, so a single-active-class player keeps their class.
    void DeclassifyFor(Player const* player, std::string& text);

    // Site-facing path for WDB-CACHED surfaces (npc_text / quest-query / page_text): replace $C/$c with a
    // client-side marker ({mcU}/{mcL}) globally (realm-gated on enable + non-empty word, no recipient). The
    // cached text is then identical for every character, and the client addon expands the marker per-viewer.
    void MarkClassToken(std::string& text);
}

#endif
