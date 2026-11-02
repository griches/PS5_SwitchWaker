#include "pc/pc_player_controls.h"
#include "d/d_com_inf_game.h"
#include "d/d_meter.h"
#include "d/actor/d_a_player_main.h"

namespace pc { bool menuOpen(); }

bool pc_player_controls_blocked() {
    auto* player = daPy_getPlayerLinkActorClass();
    return !player || pc::menuOpen() || dMenu_flag() || dComIfGp_event_runCheck() ||
        player->checkNoControll() || player->checkPlayerDemoMode() || player->mDemo.getDemoMode() != 0;
}
