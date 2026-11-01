// Scripted options-menu checks, kept separate from console presentation.
#include "pc/pc_menu_smoke.h"
#include "pc_internal.h"
#include "pc/pc_settings.h"
#include "pc/pc_gpu_opts.h"
#include "d/d_com_inf_game.h"
#include "d/d_save.h"
#include "d/d_item_data.h"
#include "d/actor/d_a_player.h"
#include "m_Do/m_Do_MemCard.h"
#include "m_Do/m_Do_MemCardRWmng.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
namespace pc {
namespace {
// ---- COS_SMOKE=options-menu ----------------------------------------------------------------------

struct Expect {
    std::string kind;
    std::string a;
    std::string b;
};

struct Smoke {
    bool on = false;
    unsigned int endFrame = 0;
    std::vector<Expect> expects;
    unsigned int errors = 0;
    bool havePos = false;
    float pos[3] = {};
    unsigned int openFrames = 0;
    unsigned int pausedChecks = 0;
    bool runCard = false; // "#card run"
} sSmoke;

void smokeError(const char* fmt, const std::string& a, const std::string& b = "") {
    sSmoke.errors++;
    char line[512];
    snprintf(line, sizeof(line), fmt, a.c_str(), b.c_str());
    writef(STDERR_FILENO, "[cos] options-menu: FAIL %s\n", line);
}

// "#expect <kind> <a> [<b>]" lines of the COS_INPUT script:
//   end <frame>              the test ends there (exit 0 if every check held)
//   opened <n>               the menu was opened n times
//   setting <KEY> <value>    pc_settings_get(KEY) (the value in effect)
//   saved <line>             the settings file has this line (e.g. COS_FB_SCALE@handheld=1.5)
//   efb-height <h>           the EFB's pixel height (pc_efb_pixel_size of 640x480)
//   stage <name> <room>      the PLAY scene's stage and room
//   overlay <0|1>            the FPS overlay is shown
//   alloc-failures <n>       JKR allocations that failed since the start (bug B8: 0)
//   riding <0|1>             the player rides the boat with the sail on X (Depuración > Navegar)
//   card-roundtrip <stage> [<room>]  the game saved through its save screen: the card's save file (the
//                            run's own card, "#card run") loads back (mDoMemCd_Load, checksum of
//                            the file the game uses, card_to_memory) with the items the player has now
//                            and the return place <stage> (and <room>) (bug B8)
// Other lines: "#items <hex,...>" gives the new file of the debug boot those items
// (COS_BOOT_ITEMS); "#card run" makes memory card A the empty folder <run dir>/card/, so the game
// saves there and never on the user's card.
// Always: while the menu is open in the PLAY scene, the player does not move (paused, no input) - the
// script should hold the stick then.
void smokeParse() {
    FILE* f = gConfig.input != nullptr ? fopen(gConfig.input, "r") : nullptr;
    if (f == nullptr) {
        writef(STDERR_FILENO, "[cos] options-menu: needs COS_INPUT with #expect lines\n");
        pc_exit(PC_EXIT_USAGE);
    }
    char line[512];
    while (fgets(line, sizeof(line), f) != nullptr) {
        if (strncmp(line, "#items ", 7) == 0) {
            char items[256] = {};
            if (sscanf(line + 7, "%255s", items) == 1) {
                setenv("COS_BOOT_ITEMS", items, 1);
            }
            continue;
        }
        if (strncmp(line, "#card run", 9) == 0) {
            sSmoke.runCard = true;
            continue;
        }
        if (strncmp(line, "#expect ", 8) != 0) {
            continue;
        }
        char kind[64] = {}, a[256] = {}, b[256] = {};
        if (sscanf(line + 8, "%63s %255s %255s", kind, a, b) < 2) {
            continue;
        }
        if (strcmp(kind, "end") == 0) {
            sSmoke.endFrame = (unsigned int)atoi(a);
        } else {
            sSmoke.expects.push_back({kind, a, b});
        }
    }
    fclose(f);
    if (sSmoke.endFrame == 0) {
        writef(STDERR_FILENO, "[cos] options-menu: the script has no \"#expect end <frame>\"\n");
        pc_exit(PC_EXIT_USAGE);
    }
    writef(STDERR_FILENO, "[cos] options-menu: %zu checks at frame %u; settings file %s\n", sSmoke.expects.size(),
           sSmoke.endFrame, pc_settings_path());
}

// "#expect card-roundtrip <stage>": the save file the game wrote loads back with the items the player has
// now and the return place `stage`. Blocks the game thread while the card thread reads (as the
// file select's load does over several frames); only at the test's end frame.
void checkCardRoundtrip(const std::string& stage, const std::string& room) {
    alignas(32) static u8 sLoaded[3 * sizeof(card_gamedata)];
    u8 itemsNow[dInvSlot_ItemLast_e];
    for (int slot = 0; slot < dInvSlot_ItemLast_e; slot++) {
        itemsNow[slot] = dComIfGs_getItem(slot);
    }
    const int dataNum = dComIfGs_getDataNum();
    if (!sSmoke.runCard || runCardGciPath() == nullptr || access(runCardGciPath(), R_OK) != 0) {
        smokeError("card-roundtrip: no save file %s%s (needs \"#card run\" and a save through the save screen)",
                   runCardGciPath() != nullptr ? runCardGciPath() : "(no run card)");
        return;
    }
    auto wait = [](const char* what) {
        for (int i = 0; i < 5000 && !mDoMemCd_isCardCommNone(); i++) {
            usleep(2000);
        }
        if (!mDoMemCd_isCardCommNone()) {
            smokeError("card-roundtrip: %s did not finish in 10 s%s", what);
            return false;
        }
        return true;
    };
    if (!wait("the card thread's last command")) {
        return;
    }
    memset(sLoaded, 0, sizeof(sLoaded));
    mDoMemCd_Load();
    if (!wait("the load")) {
        return;
    }
    const u32 load = mDoMemCd_LoadSync(sLoaded, sizeof(sLoaded), 0);
    if (load != 1) {
        smokeError("card-roundtrip: LoadSync %s%s", std::to_string(load));
        return;
    }
    if (!mDoMemCdRWm_TestCheckSumGameData(&sLoaded[dataNum * sizeof(card_gamedata)])) {
        smokeError("card-roundtrip: file %s fails its checksum%s", std::to_string(dataNum + 1));
        return;
    }
    dComIfGs_setCardToMemory(sLoaded, dataNum);
    int same = 0;
    for (int slot = 0; slot < dInvSlot_ItemLast_e; slot++) {
        if (dComIfGs_getItem(slot) != itemsNow[slot]) {
            char what[96];
            snprintf(what, sizeof(what), "slot %d: 0x%02x, had 0x%02x", slot, dComIfGs_getItem(slot), itemsNow[slot]);
            smokeError("card-roundtrip: loaded item %s%s", what);
        } else if (itemsNow[slot] != 0xFF) {
            same++;
        }
    }
    dSv_player_return_place_c& place = g_dComIfG_gameInfo.save.getPlayer().getPlayerReturnPlace();
    if (stage != place.getName() || (!room.empty() && place.getRoomNo() != atoi(room.c_str()))) {
        smokeError("card-roundtrip: return place %s, expected %s",
                   std::string(place.getName()) + " room " + std::to_string(place.getRoomNo()),
                   stage + (room.empty() ? "" : " room " + room));
    }
    writef(STDERR_FILENO, "[cos] options-menu: card-roundtrip: file %d loaded back, %d items as saved, return "
                          "place %s room %d point %d\n",
           dataNum + 1, same, place.getName(), place.getRoomNo(), place.getPoint());
}

bool fileHasLine(const std::string& want) {
    FILE* f = fopen(pc_settings_path(), "r");
    if (f == nullptr) {
        return false;
    }
    char line[512];
    bool found = false;
    while (!found && fgets(line, sizeof(line), f) != nullptr) {
        line[strcspn(line, "\r\n")] = '\0';
        found = want == line;
    }
    fclose(f);
    return found;
}

} // namespace
void menuSmokeInit() {
    sSmoke.on = gConfig.smoke != nullptr && strcmp(gConfig.smoke, "options-menu") == 0;
    if (sSmoke.on) smokeParse();
}
bool menuSmokeEnabled() { return sSmoke.on; }
bool menuSmokeWantsRunCard() { return sSmoke.on && sSmoke.runCard; }
void menuSmokeFrame(unsigned int frame, bool open, unsigned int opened, bool playing) {
    if (!sSmoke.on) return;
    if (open && playing) {
        fopAc_ac_c* player = dComIfGp_getPlayer(0);
        if (player != nullptr) {
            const cXyz& p = player->current.pos;
            if (sSmoke.havePos && sSmoke.openFrames >= 3) {
                const float d = std::fabs(p.x - sSmoke.pos[0]) + std::fabs(p.y - sSmoke.pos[1]) +
                                std::fabs(p.z - sSmoke.pos[2]);
                sSmoke.pausedChecks++;
                if (d > 0.01f) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%u (moved %.3f)", frame, (double)d);
                    smokeError("the player moved while the menu was open at frame %s%s", buf);
                }
            }
            sSmoke.pos[0] = p.x;
            sSmoke.pos[1] = p.y;
            sSmoke.pos[2] = p.z;
            sSmoke.havePos = true;
        }
        sSmoke.openFrames++;
    } else {
        sSmoke.havePos = false;
        sSmoke.openFrames = 0;
    }
    if (frame < sSmoke.endFrame) {
        return;
    }
    for (const Expect& e : sSmoke.expects) {
        if (e.kind == "opened") {
            if (opened != (unsigned int)atoi(e.a.c_str())) {
                smokeError("opened %s times, expected %s", std::to_string(opened), e.a);
            }
        } else if (e.kind == "setting") {
            const std::string v = pc_settings_get(e.a.c_str());
            if (v != e.b) {
                smokeError("%s", e.a + " = " + v, "");
            } else {
                writef(STDERR_FILENO, "[cos] options-menu: ok %s = %s\n", e.a.c_str(), v.c_str());
            }
        } else if (e.kind == "saved") {
            if (!fileHasLine(e.a)) {
                smokeError("the settings file has no line %s%s", e.a);
            } else {
                writef(STDERR_FILENO, "[cos] options-menu: ok file line %s\n", e.a.c_str());
            }
        } else if (e.kind == "efb-height") {
            unsigned int w = 0, h = 0;
            pc_efb_pixel_size(640, 480, &w, &h);
            if (h != (unsigned int)atoi(e.a.c_str())) {
                smokeError("EFB height %s, expected %s", std::to_string(h), e.a);
            } else {
                writef(STDERR_FILENO, "[cos] options-menu: ok EFB %ux%u\n", w, h);
            }
        } else if (e.kind == "stage") {
            const char* name = playing ? dComIfGp_getStartStageName() : "(not in PLAY)";
            const int room = playing ? dComIfGp_getStartStageRoomNo() : -1;
            if (e.a != name || room != atoi(e.b.c_str())) {
                smokeError("stage %s, expected %s", std::string(name) + " " + std::to_string(room), e.a + " " + e.b);
            } else {
                writef(STDERR_FILENO, "[cos] options-menu: ok stage %s room %d\n", name, room);
            }
        } else if (e.kind == "riding") {
            // The sailing preset (Navegar): the player on the boat, the sail on X.
            const bool riding = playing && dComIfGp_checkPlayerStatus0(0, daPyStts0_SHIP_RIDE_e) != 0 &&
                                dComIfGp_getShipActor() != nullptr;
            const bool sail = dComIfGs_getSelectItem(dItemBtn_X_e) != dInvSlot_NONE_e &&
                              dComIfGs_getItem(dComIfGs_getSelectItem(dItemBtn_X_e)) == dItemNo_SAIL_e;
            if ((riding && sail ? 1 : 0) != atoi(e.a.c_str())) {
                smokeError("riding %s, expected %s", std::string(riding ? "1" : "0") + (sail ? " (sail on X)" : " (no sail on X)"),
                           e.a);
            } else {
                writef(STDERR_FILENO, "[cos] options-menu: ok riding %s\n", e.a.c_str());
            }
        } else if (e.kind == "overlay") {
            if ((gConfig.fpsOverlay ? 1 : 0) != atoi(e.a.c_str())) {
                smokeError("overlay %s, expected %s", gConfig.fpsOverlay ? "1" : "0", e.a);
            }
        } else if (e.kind == "alloc-failures") {
            if (heapAllocFailures() != (unsigned int)atoi(e.a.c_str())) {
                smokeError("%s JKR allocations failed, expected %s", std::to_string(heapAllocFailures()), e.a);
            } else {
                writef(STDERR_FILENO, "[cos] options-menu: ok %u failed JKR allocations\n", heapAllocFailures());
            }
        } else if (e.kind == "card-roundtrip") {
            checkCardRoundtrip(e.a, e.b);
        } else {
            smokeError("unknown #expect %s%s", e.kind);
        }
    }
    if (sSmoke.pausedChecks == 0 && opened > 0) {
        smokeError("no frame with the menu open in the PLAY scene was checked%s%s", "");
    }
    writef(STDERR_FILENO, "[cos] options-menu: %s at frame %u (%u errors, %u paused frames checked)\n",
           sSmoke.errors == 0 ? "PASS" : "FAIL", frame, sSmoke.errors, sSmoke.pausedChecks);
    pc_exit(sSmoke.errors == 0 ? PC_EXIT_REACHED : PC_EXIT_CHECK_FAILED);
}

} // namespace pc
