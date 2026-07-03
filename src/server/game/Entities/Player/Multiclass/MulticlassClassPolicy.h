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

#ifndef ACORE_MULTICLASS_CLASS_POLICY_H
#define ACORE_MULTICLASS_CLASS_POLICY_H

#include "UnitDefines.h"

// Per-context class-check policy for Player::IsClass. A check is PROJECTED-ONLY (matches only the
// slot-0 projected class) for the character's one-time creation/origin identity family; every other
// context is ANY-ACTIVE (matches any class in the active set). Pure -> unit-tested.
[[nodiscard]] constexpr bool MulticlassIsProjectedOnlyContext(ClassContext context)
{
    switch (context)
    {
        case CLASS_CONTEXT_INIT:              // character creation identity (DK/Ebon Hold onboarding)
        case CLASS_CONTEXT_TELEPORT:          // origin teleport gating
        case CLASS_CONTEXT_TALENT_POINT_CALC: // starting talent-point grant
            return true;
        default:
            return false;
    }
}

#endif // ACORE_MULTICLASS_CLASS_POLICY_H
