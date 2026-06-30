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

#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "PlayerScript.h"

class multiclass_playerscript : public PlayerScript
{
public:
    multiclass_playerscript() : PlayerScript("multiclass_playerscript", { PLAYERHOOK_ON_LOGIN }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", false))
            return;

        LOG_INFO("module.multiclass", "mod-multiclass loaded for player {}", player->GetGUID().ToString());
    }
};

void Addmod_multiclassScripts()
{
    new multiclass_playerscript();
}
