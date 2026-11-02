// Exercise the real parser/resolver/writer. Only platform logging/path lookup and
// explicit I/O failure injection are substituted; no settings implementation is mocked.
#include "pc/pc_settings.h"
#include "pc/pc_menu_policy.h"
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <string>
#include <unistd.h>
namespace { bool failBackup, failRename, failFlush; }
extern "C" int __real_open(const char*, int, ...);
extern "C" int __wrap_open(const char* path, int flags, ...) {
    if (failBackup && std::strstr(path, ".pre-ps5-profile.bak")) { errno = EACCES; return -1; }
    if (!(flags & O_CREAT)) return __real_open(path, flags);
    va_list args; va_start(args, flags); const int mode = va_arg(args, int); va_end(args);
    return __real_open(path, flags, mode);
}
extern "C" int __real_rename(const char*, const char*);
extern "C" int __wrap_rename(const char* a, const char* b) {
    if (failRename) { errno = EIO; return -1; }
    return __real_rename(a, b);
}
extern "C" int __real_fflush(FILE*);
extern "C" int __wrap_fflush(FILE* file) { if (failFlush) { errno = EIO; return EOF; } return __real_fflush(file); }
namespace pc {
void executablePath(char* out, std::size_t size) { if (size) *out = 0; }
bool menuBuiltinSetting(const char* key) { return std::strcmp(key, "COS_FB_SCALE") == 0; }
void writef(int, const char*, ...) {}
}
std::string read(const std::string& path) { std::ifstream f(path); return {std::istreambuf_iterator<char>(f), {}}; }
void write(const std::string& path, const std::string& text) { std::ofstream f(path); f << text; assert(f.good()); }
void expect(const char* key, const char* value) { assert(std::string(pc_settings_get(key)) == value); }
int main(int argc, char** argv) {
    assert(pc_menu_setting_order("COS_PS5_OUTPUT", 71) < pc_menu_setting_order("COS_FB_SCALE", 10));
    assert(pc_menu_setting_order("COS_HD_MAX_SIZE", 81) < pc_menu_setting_order("COS_HD_DISTANT_ISLANDS", 65));
    assert(pc_menu_setting_order("COS_DOF", 50) < pc_menu_setting_order("COS_HD_FAR_BLUR", 75));
    assert(pc_menu_setting_order("COS_HD_TORCH_LIGHT", 74) < pc_menu_setting_order("COS_MENU_TEXT_SIZE", 950));

    assert(argc == 3);
    const std::string test = argv[1], path = std::string(argv[2]) + "/settings.ini", backup = path + ".pre-ps5-profile.bak";
    for (auto key : {"COS_FB_SCALE", "COS_DYNRES", "COS_HD_MAX_SIZE", "COS_FPS60", "COS_OPERATION_MODE"}) unsetenv(key);
    setenv("COS_SETTINGS", path.c_str(), 1);
    setenv("COS_OPERATION_MODE", "docked", 1);
    std::string fixture = "# legacy\nCOS_FB_SCALE=1.0\nCOS_FB_SCALE@handheld=1.5\nCOS_FB_SCALE@docked=2.25\nCOS_HD_MAX_SIZE@docked=auto\nCOS_FPS60@docked=0\nUNRELATED@handheld=keep\n[dev]\n# keep this comment\nMESA_TEST=unchanged\n";
    if (test == "plain") fixture = "COS_FB_SCALE=3.0\nCOS_HD_MAX_SIZE=full\n";
    if (test == "inactive") fixture = "COS_FB_SCALE@handheld=1.5\n";
    if (test == "active") fixture = "COS_FB_SCALE@docked=3.0\nCOS_HD_MAX_SIZE@docked=256\n";
    if (test == "handheld") { setenv("COS_OPERATION_MODE", "handheld", 1); }
    if (test == "dev-handheld") fixture += "COS_OPERATION_MODE=handheld\n";
    if (test == "locked") setenv("COS_FB_SCALE", "9", 1);
    if (test == "backup-failure") failBackup = true;
    if (test == "rename-failure") failRename = true;
    if (test == "flush-failure") failFlush = true;
    if (test != "fresh") write(path, fixture);
    pc_settings_load_early(); pc_settings_load_early();
    // Values consumed before registration are already flattened, just like startup.
    if (test != "fresh" && test != "inactive")
        assert(std::string(getenv("COS_FB_SCALE")) == (test == "locked" ? "9" : test == "plain" || test == "active" ? "3.0" : test == "handheld" || test == "dev-handheld" ? "1.5" : "2.25"));
    assert(pc_settings_mode() == PC_MODE_DOCKED);
    for (auto key : {"COS_FB_SCALE", "COS_DYNRES", "COS_HD_MAX_SIZE", "COS_FPS60"}) {
        const char* fallback = std::strcmp(key, "COS_FB_SCALE") == 0 ? "0" : std::strcmp(key, "COS_FPS60") == 0 ? "1" : "auto";
        setenv(key, fallback, 0); // PS5 defaults are applied after early file loading.
        PcSettingDesc desc = {}; desc.key = key; desc.defaultValue = fallback;
        desc.flags = PC_SETTING_PER_MODE; // PS5 strips legacy mode flags even for stale descriptors.
        pc_settings_register(&desc);
    }
    assert((pc_settings_at(0)->flags & PC_SETTING_PER_MODE) == 0);
    expect("COS_FB_SCALE", test == "locked" ? "9" : test == "fresh" || test == "inactive" ? "0" : test == "plain" || test == "active" ? "3.0" : test == "handheld" || test == "dev-handheld" ? "1.5" : "2.25");
    expect("COS_HD_MAX_SIZE", test == "plain" ? "full" : test == "active" ? "256" : test == "handheld" || test == "dev-handheld" ? "512" : "auto");
    expect("COS_FPS60", test == "plain" || test == "fresh" || test == "inactive" || test == "active" || test == "handheld" || test == "dev-handheld" ? "1" : "0");
    setenv("COS_OPERATION_MODE", "handheld", 1);
    for (int i = 0; i < 30; ++i) pc_settings_poll_mode();
    assert(pc_settings_mode() == PC_MODE_DOCKED);
    unsetenv("COS_OPERATION_MODE");
    if (failBackup || failRename || failFlush) {
        assert(read(path) == fixture);
        assert(pc_settings_set("COS_FB_SCALE", "4.5") == -1);
        assert(read(path) == fixture);
        failBackup = failRename = failFlush = false;
        pc_settings_reload();
    }
    const bool migrated = test != "plain" && test != "fresh";
    if (migrated) {
        assert(read(backup) == fixture);
        const auto normalized = read(path);
        assert(normalized.find("COS_FB_SCALE@") == std::string::npos);
        assert(normalized.find("COS_OPERATION_MODE") == std::string::npos);
        if (test != "inactive" && test != "active") {
            assert(normalized.find("UNRELATED@handheld=keep") != std::string::npos);
            assert(normalized.find("# keep this comment") != std::string::npos);
            assert(normalized.find("MESA_TEST=unchanged") != std::string::npos);
        }
        pc_settings_reload();
        assert(read(path) == normalized); // no rewrite or new backup on a repeated load
        assert(!std::filesystem::exists(backup + ".1"));
    } else assert(!std::filesystem::exists(backup));
    if (test == "locked") {
        assert(pc_settings_locked("COS_FB_SCALE"));
        assert(read(path).find("COS_FB_SCALE=2.25") != std::string::npos);
        assert(read(path).find("COS_FB_SCALE=9") == std::string::npos);
    } else {
        assert(pc_settings_set_mode("COS_FB_SCALE", PC_MODE_HANDHELD, "4.5") == 0);
        expect("COS_FB_SCALE", "4.5");
        assert(read(path).find("COS_FB_SCALE=4.5") != std::string::npos);
        if (migrated) {
            const std::string copied = "COS_FB_SCALE@docked=3.0\n";
            write(path, copied); pc_settings_reload(); expect("COS_FB_SCALE", "3.0");
            assert(read(backup + ".1") == copied); // never overwrite the original backup
            assert(read(backup) == fixture);
        }
    }
    PcSettingDesc desc = {}; desc.key = "COS_FPS60";
    assert(pc_menu_setting_visible(&desc));
    for (auto key : {"COS_DYNRES", "COS_GPU_GROUPS", "COS_PRECOMPILE", "COS_PRECOMPILE_SCREEN"}) {
        desc.key = key; assert(!pc_menu_setting_visible(&desc));
    }
    desc.key = "COS_SWITCH_GPU_PROFILE"; desc.flags = PC_SETTING_SWITCH_ONLY;
    assert(!pc_menu_setting_visible(&desc)); assert(!pc_menu_setting_visible(nullptr));

    // Reset clears saved overrides and reapplies even unchanged live values.
    static int applied = 0, subscribed = 0;
    static std::string live;
    PcSettingDesc transparency = {};
    transparency.key = "COS_MENU_TRANSPARENCY"; transparency.defaultValue = "40";
    transparency.apply = [](const char*, const char* value, void*) { live = value; ++applied; };
    pc_settings_register(&transparency);
    pc_settings_subscribe(transparency.key, [](const char*, const char*, void*) { ++subscribed; }, nullptr);
    pc_settings_set_mode_default("COS_HD_TEXTURES", PC_MODE_DOCKED, "1");
    PcSettingDesc textures = {}; textures.key = "COS_HD_TEXTURES"; textures.defaultValue = "0";
    pc_settings_register(&textures);
    write(path, "COS_MENU_TRANSPARENCY=80\nCOS_HD_TEXTURES=0\nUNKNOWN=keep\n[dev]\nMESA_TEST=old\n");
    pc_settings_reload();
    assert(live == "80");
    // Saving and clearing must report I/O errors without adopting unsaved values.
    for (int failure = 0; failure < 3; ++failure) {
        const auto saved = read(path);
        const int beforeApplied = applied, beforeSubscribed = subscribed;
        failRename = failure == 0;
        failFlush = failure == 1;
        if (failure == 2) std::filesystem::create_directory(path + ".tmp");
        assert(pc_settings_set(transparency.key, "60") == -1);
        expect(transparency.key, "80");
        assert(live == "80" && applied == beforeApplied && subscribed == beforeSubscribed);
        assert(pc_settings_clear(transparency.key) == -1);
        expect(transparency.key, "80");
        assert(live == "80" && applied == beforeApplied && subscribed == beforeSubscribed);
        assert(read(path) == saved);
        failRename = failFlush = false;
        std::filesystem::remove_all(path + ".tmp");
    }
    applied = subscribed = 0;
    assert(pc_settings_restore_defaults() == 0);
    expect("COS_MENU_TRANSPARENCY", "40"); expect("COS_HD_TEXTURES", "1");
    expect("UNKNOWN", "");
    assert(live == "40" && applied == 1 && subscribed == 1);
    assert(!std::filesystem::exists(path));
    live = "stale";
    assert(pc_settings_restore_defaults() == 0); // Missing file; unchanged default still reapplied.
    assert(live == "40" && applied == 2 && subscribed == 2);
    expect("COS_FB_SCALE", test == "locked" ? "9" : "0");
    assert(pc_settings_set("COS_HD_TEXTURES", "0") == 0);
    assert(read(path).find("UNKNOWN") == std::string::npos);
    assert(read(path).find("\n[dev]\n") == std::string::npos);
    std::filesystem::rename(path, path + ".reset-test");
    std::filesystem::create_directory(path);
    write(path + "/block", "keep");
    assert(pc_settings_restore_defaults() == -1);
    expect("COS_HD_TEXTURES", "0");
    assert(applied == 2 && subscribed == 2);
    std::filesystem::remove_all(path);
    std::filesystem::rename(path + ".reset-test", path);
    assert(pc_settings_restore_defaults() == 0);
    expect("COS_HD_TEXTURES", "1");
    assert(applied == 3 && subscribed == 3);
}
