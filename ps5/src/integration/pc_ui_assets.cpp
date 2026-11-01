// Individual controller textures/layouts; no DVD overlays or replacement archives.
// SPDX-License-Identifier: MIT
#include "pc/pc_ui_assets.h"
#include "pc/pc_ui_transform.h"
#include "pc/pc_disc_region.h"
#include "JSystem/JKernel/JKRArchive.h"
#include <dolphin/gx.h>
#include <aurora/texture.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <span>

namespace {
constexpr uint32_t kMaxResources = 32, kMaxBundleSize = 1024 * 1024;
struct Resource {
    const char* volume{};
    const char* name{};
    unsigned char* data{};
    uint32_t type{}, size{}, originalSize{}, originalCrc{};
    unsigned char* transformed{};
    uint32_t transformedSize{};
};
struct ArchiveState { const JKRArchive* archive{}; bool valid{}; bool menu{}; };
std::array<Resource, kMaxResources> resources;
std::array<ArchiveState, 16> archives;
uint32_t count;
std::atomic_bool ready{}, menuActive{};
std::mutex archiveMutex;
thread_local bool validating;
uint32_t be32(const unsigned char* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
uint16_t be16(const unsigned char* p) { return uint16_t(p[0]) << 8 | p[1]; }
uint32_t crc(const void* ptr, uint32_t size) {
    auto* bytes = static_cast<const unsigned char*>(ptr);
    uint32_t result = ~0u;
    for (uint32_t i = 0; i < size; ++i) {
        result ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) result = result >> 1 ^ (0xedb88320u & (0u - (result & 1u)));
    }
    return ~result;
}
const Resource* find(const char* volume, uint32_t type, const char* path) {
    if (!volume || !path) return nullptr;
    while (*path == '/') ++path;
    const char* slash = std::strchr(path, '/');
    if (slash) {
        const bool texture = slash - path == 4 && std::memcmp(path, "timg", 4) == 0;
        const bool layout = slash - path == 4 && std::memcmp(path, "scrn", 4) == 0;
        if (!texture && !layout) return nullptr;
        const uint32_t directoryType = texture ? 0x54494d47u : 0x5343524eu;
        if (type && type != 0x3f3f3f3fu && type != directoryType) return nullptr;
        type = directoryType;
        path = slash + 1;
        if (std::strchr(path, '/')) return nullptr;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const auto& item = resources[i];
        if (std::strcmp(volume, item.volume) == 0 && std::strcmp(path, item.name) == 0 &&
            (!type || type == 0x3f3f3f3fu || type == item.type)) return &item;
    }
    return nullptr;
}
// Validate all resources of this archive before returning even the first replacement.
// Reentrant original lookups bypass this adapter; no source pointers are retained.
bool validate(JKRArchive* archive, const char* volume) {
    struct Scope { Scope() { validating = true; } ~Scope() { validating = false; } } scope;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& item = resources[i];
        if (std::strcmp(volume, item.volume) != 0) continue;
        void* original = archive->getResource(item.type, item.name);
        if (!item.originalSize) {
            if (original) return false; // The one added resource must really be absent.
        } else if (!original || archive->getExpandedResSize(original) != item.originalSize ||
                   crc(original, item.originalSize) != item.originalCrc) return false;
    }
    // All checks passed before constructing any replacement layout. Results are
    // process-owned and reusable because the original sizes and CRCs match.
    std::array<const char*, kMaxResources> names{};
    size_t nameCount = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (resources[i].type == 0x54494d47u) names[nameCount++] = resources[i].name;
    for (uint32_t i = 0; i < count; ++i) {
        auto& item = resources[i];
        if (std::strcmp(volume, item.volume) || item.type != 0x5343524eu || item.transformed) continue;
        const auto* original = static_cast<const uint8_t*>(archive->getResource(item.type, item.name));
        std::vector<uint8_t> output;
        if (!pc_ui_transform_layout({original, item.originalSize}, {item.data, item.size},
                                    {names.data(), nameCount}, output)) return false;
        auto* data = static_cast<unsigned char*>(std::aligned_alloc(32, (output.size() + 31u) & ~size_t(31)));
        if (!data) return false;
        std::memcpy(data, output.data(), output.size());
        item.transformed = data;
        item.transformedSize = static_cast<uint32_t>(output.size());
    }
    return true;
}
void* replacement(const Resource& item) {
    return item.type == 0x5343524eu ? item.transformed : item.data;
}
}

void pc_ui_assets_init() {
    if (ready.load(std::memory_order_acquire)) return;
    FILE* file = std::fopen(pc_disc_is_pal() ? "/app0/res/resources-pal.bin" :
                                            "/app0/res/resources-us.bin", "rb");
    if (!file) return;
    unsigned char header[32]{};
    bool valid = std::fread(header, 1, sizeof(header), file) == sizeof(header) &&
        std::memcmp(header, "P5UIRES3", 8) == 0;
    const uint32_t entries = be32(header + 8), size = be32(header + 12);
    valid = valid && entries > 0 && entries <= kMaxResources && size >= 32 + entries * 128 && size <= kMaxBundleSize;
    auto* bytes = valid ? static_cast<unsigned char*>(std::aligned_alloc(32, (size + 31u) & ~31u)) : nullptr;
    valid = valid && bytes;
    if (valid) {
        std::memcpy(bytes, header, 32);
        valid = std::fread(bytes + 32, 1, size - 32, file) == size - 32 && std::fgetc(file) == EOF &&
            !std::ferror(file) && crc(bytes + 32, size - 32) == be32(header + 16);
    }
    std::fclose(file);
    for (uint32_t i = 0; valid && i < entries; ++i) {
        auto* record = bytes + 32 + i * 128;
        const uint32_t type = be32(record + 64), offset = be32(record + 68), length = be32(record + 72);
        valid = std::memchr(record, 0, 16) && std::memchr(record + 16, 0, 48) &&
            (std::strcmp(reinterpret_cast<char*>(record), "menures") == 0 ||
             std::strcmp(reinterpret_cast<char*>(record), "file_select") == 0 ||
             std::strcmp(reinterpret_cast<char*>(record), "prosperowake") == 0) &&
            (type == 0x54494d47u || type == 0x5343524eu) && length >= 32 &&
            offset >= 32 + entries * 128 && offset % 32 == 0 && offset <= size && length <= size - offset;
        if (!valid) break;
        auto* data = bytes + offset;
        valid = crc(data, length) == be32(record + 76);
        if (type == 0x54494d47u) {
            const uint32_t image = be32(data + 28), w = be16(data + 2), h = be16(data + 4);
            valid = valid && data[0] == 5 && w && h && w <= 1024 && h <= 1024 && data[24] == 1 && image >= 32 && image % 32 == 0 &&
                image <= length && ((w + 3) / 4) * ((h + 3) / 4) * 32 <= length - image;
        } else valid = valid && std::memcmp(data, "P5UILAY1", 8) == 0 &&
            be32(data + 8) <= 128 && length == 32 + be32(data + 8) * 64;
        resources[i] = {reinterpret_cast<char*>(record), reinterpret_cast<char*>(record + 16), data,
                        type, length, be32(record + 80), be32(record + 84)};
        for (uint32_t previous = 0; valid && previous < i; ++previous) {
            valid = std::strcmp(resources[previous].volume, resources[i].volume) != 0 ||
                    std::strcmp(resources[previous].name, resources[i].name) != 0;
        }
    }
    if (!valid) {
        std::free(bytes);
        resources = {};
        std::fprintf(stderr, "[cos] controller UI: invalid resource bundle; using disc resources\n");
        return;
    }
    count = entries;
    // Bundle storage is aligned system memory, immutable and retained for this process.
    // Only these selected texture pointers outrank optional HD texture packs.
    for (uint32_t i = 0; i < count; ++i) {
        const auto& item = resources[i];
        if (item.type != 0x54494d47u ||
            (std::strcmp(item.volume, "prosperowake") == 0 && std::strcmp(item.name, "camera_self.bti") == 0)) continue;
        auto* image = item.data + be32(item.data + 28);
        aurora::texture::register_replacement(aurora::texture::TexturePointerKey{image},
            {std::span<const uint8_t>(image, item.size - be32(item.data + 28)),
             be16(item.data + 2), be16(item.data + 4), 1, item.data[0], item.name}, {.priority = 1000});
    }
    ready.store(true, std::memory_order_release);
    std::fprintf(stderr, "[cos] controller UI: %u targeted resources loaded\n", count);
}

void* pc_ui_assets_resource(JKRArchive* archive, const char* volume, uint32_t type, const char* path) {
    if (!ready.load(std::memory_order_acquire) || validating) return nullptr;
    const auto* item = find(volume, type, path);
    if (!item) return nullptr;
    std::lock_guard lock(archiveMutex);
    ArchiveState* slot = nullptr;
    for (auto& state : archives) {
        if (state.archive == archive) return state.valid ? replacement(*item) : nullptr;
        if (!state.archive && !slot) slot = &state;
    }
    if (!slot) return nullptr; // Bounded cache; retain stock behavior if exhausted.
    const bool valid = validate(archive, volume);
    *slot = {archive, valid, std::strcmp(volume, "menures") == 0};
    if (valid && slot->menu) menuActive.store(true, std::memory_order_release);
    std::fprintf(stderr, "[cos] controller UI: %s resources %s\n", volume, valid ? "validated" : "differ from stock; left unchanged");
    return valid ? replacement(*item) : nullptr;
}
uint32_t pc_ui_assets_size(const void* resource) {
    if (!ready.load(std::memory_order_acquire) || validating) return 0;
    std::lock_guard lock(archiveMutex);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& item = resources[i];
        if (resource && resource == item.transformed) return item.transformedSize;
        if (resource == item.data && item.type == 0x54494d47u) return item.size;
    }
    return 0;
}
uint32_t pc_ui_assets_copy(const void* resource, void* buffer, uint32_t capacity) {
    const uint32_t size = std::min(pc_ui_assets_size(resource), capacity);
    if (size && buffer) std::memcpy(buffer, resource, size);
    return buffer ? size : 0;
}
void pc_ui_assets_forget(const JKRArchive* archive) {
    if (!ready.load(std::memory_order_acquire)) return;
    std::lock_guard lock(archiveMutex);
    bool menu = false;
    for (auto& state : archives) {
        if (state.archive == archive) state = {};
        menu = menu || (state.archive && state.valid && state.menu);
    }
    menuActive.store(menu, std::memory_order_release);
}
bool pc_ui_assets_active() { return menuActive.load(std::memory_order_acquire); }


void* pc_ui_assets_camera_prompt(const void* original, uint32_t size) {
    if (!ready.load(std::memory_order_acquire) || !original) return nullptr;
    std::lock_guard lock(archiveMutex);
    const auto* item = find("prosperowake", 0x54494d47u, "camera_self.bti");
    if (!item) return nullptr;
    // The composed texture outlives the game's Always archive and is built once.
    static void* camera;
    if (camera) return camera;
    std::vector<uint8_t> output;
    if (!pc_ui_compose_camera({static_cast<const uint8_t*>(original), size}, {item->data, item->size}, output))
        return nullptr;
    camera = std::aligned_alloc(32, (output.size() + 31u) & ~size_t(31));
    if (!camera) return nullptr;
    std::memcpy(camera, output.data(), output.size());
    auto* image = static_cast<uint8_t*>(camera) + 32;
    aurora::texture::register_replacement(aurora::texture::TexturePointerKey{image},
        {std::span<const uint8_t>(image, output.size() - 32), 128, 32, 1, 5, "camera_self.bti"}, {.priority = 1000});
    return camera;
}

void* pc_ui_assets_zoom_arrow() {
    if (!ready.load(std::memory_order_acquire)) return nullptr;
    const auto* item = find("prosperowake", 0x54494d47u, "zoom_arrow.bti");
    return item ? item->data : nullptr;
}
