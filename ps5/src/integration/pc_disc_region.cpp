#include "pc/pc_disc_region.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include <dirent.h>
#include <strings.h>

namespace {
PcDiscRegion probe(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return PcDiscRegion::Unsupported;
    unsigned char header[32]{};
    bool valid = std::fread(header, 1, sizeof(header), file) == sizeof(header);
    if (valid && std::memcmp(header, "CISO", 4) == 0) {
        valid = header[8] && std::fseek(file, 0x8000, SEEK_SET) == 0 &&
            std::fread(header, 1, sizeof(header), file) == sizeof(header);
    }
    std::fclose(file);
    valid = valid && header[6] == 0 && header[7] == 0 && header[28] == 0xc2 &&
        header[29] == 0x33 && header[30] == 0x9f && header[31] == 0x3d;
    if (valid && std::memcmp(header, "GZLE01", 6) == 0) return PcDiscRegion::USA;
    if (valid && std::memcmp(header, "GZLP01", 6) == 0) return PcDiscRegion::PAL;
    return PcDiscRegion::Unsupported;
}

struct Disc {
    PcDiscRegion region = PcDiscRegion::Unsupported;
    char path[1024]{};
    Disc() {
        // An explicit image always wins, including when invalid: report that image's error.
        if (const char* explicitPath = std::getenv("COS_DISC")) {
            if (std::snprintf(path, sizeof(path), "%s", explicitPath) >= int(sizeof(path))) {
                path[0] = '\0';
                return;
            }
            region = probe(path);
            return;
        }
        for (const char* directory : {"/app0", "/mnt/usb0", "/mnt/usb1"}) {
            DIR* dir = opendir(directory);
            if (!dir) continue;
            while (const dirent* entry = readdir(dir)) {
                const char* extension = std::strrchr(entry->d_name, '.');
                if (!extension || (strcasecmp(extension, ".iso") && strcasecmp(extension, ".gcm") &&
                                   strcasecmp(extension, ".ciso"))) continue;
                char candidate[sizeof(path)];
                if (std::snprintf(candidate, sizeof(candidate), "%s/%s", directory, entry->d_name) >=
                    int(sizeof(candidate))) continue;
                const auto candidateRegion = probe(candidate);
                if (candidateRegion != PcDiscRegion::Unsupported &&
                    (!path[0] || std::strcmp(candidate, path) < 0)) {
                    std::strcpy(path, candidate);
                    region = candidateRegion;
                }
            }
            closedir(dir);
            if (path[0]) return;
        }
    }
};

const Disc& disc() {
    // Function-local initialization is safe even when a game static initializer asks first.
    // Only libc is used; no renderer, settings registry or game heap is needed here.
    static const Disc selected;
    return selected;
}
} // namespace

PcDiscRegion pc_disc_region() { return disc().region; }
const char* pc_disc_path() { return disc().path; }
const char* pc_disc_game_id() { return pc_disc_is_pal() ? "GZLP01" : "GZLE01"; }
bool pc_disc_matches_game(const char* path, const char* gameId) {
    const auto region = probe(path);
    return (region == PcDiscRegion::USA && std::strcmp(gameId, "GZLE01") == 0) ||
           (region == PcDiscRegion::PAL && std::strcmp(gameId, "GZLP01") == 0);
}
