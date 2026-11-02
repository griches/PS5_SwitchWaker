#!/usr/bin/env python3
"""Host regression test of the actual platform/SDL input functions, with fake SDK calls."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
platform = (ROOT / 'ps5/src/platform.c').read_text()
driver = (ROOT / 'ps5/sdl/SDL_ps5joystick.c').read_text()
# Keep production control flow; replace only console and SDL service boundaries.
platform_source = platform[platform.index('int sceSystemServiceHideSplashScreen'):platform.index('void\nplatform_init')]
platform_source += platform[platform.index('struct pad_sample {'):platform.index('int\npad_player_readings')]
driver_source = driver[driver.index('static const uint32_t ps5_buttons'):driver.index('static int PS5_JoystickGetCount')]
driver_source += driver[driver.index('static Sint16 PS5_Stick'):driver.index('static void PS5_JoystickClose')]
preamble = r'''
#include "platform.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>
typedef uint64_t Uint64;
typedef uint8_t Uint8;
typedef int16_t Sint16;
typedef uint32_t SDL_JoystickID;
typedef struct { SDL_JoystickID instance_id; } SDL_Joystick;
#define SDL_arraysize(a) (sizeof(a)/sizeof((a)[0]))
#define SDL_clamp(x,a,b) ((x)<(a)?(a):(x)>(b)?(b):(x))
static Uint64 ticks;
static bool user_ready, device_ready, shell, connected = true, cross;
static int reads, opens, added, removed;
static bool sent_cross;
static Sint16 axes[6];
static Uint64 SDL_GetTicks(void) { return ticks; }
static Uint64 SDL_GetTicksNS(void) { return ticks * 1000000; }
static void SDL_PrivateJoystickAdded(SDL_JoystickID id) { assert(id == 1); ++added; }
static void SDL_PrivateJoystickRemoved(SDL_JoystickID id) { assert(id == 1); ++removed; }
static void SDL_SendJoystickButton(Uint64 t, SDL_Joystick* j, Uint8 b, bool down) { if (!b) sent_cross = down; }
static void SDL_SendJoystickAxis(Uint64 t, SDL_Joystick* j, Uint8 a, Sint16 value) { axes[a] = value; }
'''
mocks = r'''
void say(const char *format, ...) {}
double now_seconds(void) { return ticks / 1000.0; }
int sceUserServiceInitialize(const void *params) { return 0; }
int sceUserServiceGetInitialUser(int32_t *user) { *user = 1; return user_ready ? 0 : -1; }
int sceUserServiceGetLoginUserIdList(struct user_list *list) { *list = (struct user_list){{1,-1,-1,-1}}; return 0; }
int scePadInit(void) { return 0; }
int scePadOpen(int32_t user, int32_t port, int32_t index, const void *params) { ++opens; return device_ready ? 7 : -1; }
int scePadGetHandle(int32_t user, int32_t port, int32_t index) { return -1; }
int scePadClose(int32_t handle) { return 0; }
int scePadSetVibrationMode(int32_t handle, int32_t mode) { return 0; }
int sceKernelUsleep(uint32_t us) { return 0; }
int scePadRead(int32_t handle, void *samples, int32_t capacity) {
    ++reads;
    *(struct pad_sample*)samples = (struct pad_sample){
        .buttons = (cross ? PAD_CROSS : 0) | (shell ? PAD_INTERCEPTED : 0),
        .left_x = cross ? 255 : 128, .left_y = 128, .right_x = 128, .right_y = 128,
        .l2 = cross ? 255 : 0, .connected = connected};
    return 1;
}
int main(void) {
    assert(PS5_JoystickInit()); // Service unavailable at startup.
    assert(!ps5_pad_opened && ps5_devices == 0);
    ticks = 500; user_ready = true;
    PS5_JoystickDetect();
    assert(!ps5_pad_opened); // Retry is bounded, not a busy loop.
    ticks = 1000;
    PS5_JoystickDetect(); // Service ready, device still absent.
    assert(ps5_pad_opened && !ps5_devices);
    ticks = 2001; device_ready = true; cross = true;
    PS5_JoystickDetect();
    assert(ps5_devices == 1 && added == 1);
    SDL_Joystick joystick = {1};
    int before = reads;
    cross = false; // Changed after Detect: Update must deliver this sample now.
    PS5_JoystickUpdate(&joystick);
    assert(reads == before + 1 && !sent_cross && axes[0] == 0);
    PS5_JoystickUpdate(&joystick); // Multiple open devices share the poll.
    PS5_JoystickDetect();
    assert(reads == before + 1);
    cross = true;
    PS5_JoystickUpdate(&joystick);
    assert(sent_cross && axes[0] > 0 && axes[4] > 0);
    PS5_JoystickDetect();
    shell = true;
    PS5_JoystickUpdate(&joystick);
    assert(!sent_cross && axes[0] == 0 && axes[4] == -32768);
    PS5_JoystickDetect();
    shell = false; cross = false;
    PS5_JoystickUpdate(&joystick);
    assert(!sent_cross && axes[0] == 0);
    PS5_JoystickDetect();
    connected = false;
    PS5_JoystickUpdate(&joystick);
    assert(!sent_cross && axes[0] == 0);
    PS5_JoystickDetect();
    assert(!ps5_devices && removed == 1);
    connected = true;
    PS5_JoystickDetect();
    assert(ps5_devices == 1 && added == 2);
    int previous_opens = opens;
    assert(pad_open()); // Reinitialization must not discard live handles.
    assert(opens == previous_opens);
}
'''
with tempfile.TemporaryDirectory(prefix='prosperowake-input-test-') as temporary:
    folder = Path(temporary)
    source = folder / 'input.c'
    source.write_text(preamble + platform_source + driver_source + mocks)
    executable = folder / 'input'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-I', str(ROOT / 'ps5/src'), str(source), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('Controller input passed: startup retry, hotplug, fresh samples, shared polling and system interception.')
