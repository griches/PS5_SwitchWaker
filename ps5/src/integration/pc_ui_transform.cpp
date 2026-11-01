// Controller UI edits applied to resources from the player's disc.
// SPDX-License-Identifier: MIT
#include "pc/pc_ui_transform.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace {
uint32_t readBE32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
uint16_t readBE16(const uint8_t* p) { return uint16_t(p[0]) << 8 | p[1]; }
void put32(uint8_t* p, uint32_t n) {
    p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n;
}
void put16(uint8_t* p, uint16_t n) { p[0] = n >> 8; p[1] = n; }
bool range(size_t at, size_t length, size_t size) { return at <= size && length <= size - at; }
}

bool pc_ui_transform_layout(std::span<const uint8_t> original, std::span<const uint8_t> rules,
                            std::span<const char* const> textures, std::vector<uint8_t>& output) {
    output.clear();
    if (original.size() < 32 || std::memcmp(original.data(), "SCRNblo1", 8) ||
        rules.size() < 32 || std::memcmp(rules.data(), "P5UILAY1", 8)) return false;
    const uint32_t count = readBE32(rules.data() + 8);
    if (count > 128 || rules.size() != 32 + size_t(count) * 64) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const auto* rule = rules.data() + 32 + i * 64;
        if (rule[12] & ~3u || !std::memchr(rule + 16, 0, 48)) return false;
        for (uint32_t j = 0; j < i; ++j)
            if (!std::memcmp(rule, rules.data() + 32 + j * 64, 4)) return false;
    }
    output.assign(original.begin(), original.begin() + 32);
    size_t at = 32;
    while (range(at, 8, original.size())) {
        const auto* block = original.data() + at;
        const uint32_t size = readBE32(block + 4);
        if (size < 8 || !range(at, size, original.size())) return false;
        const size_t start = output.size();
        if (!std::memcmp(block, "PIC1", 4)) {
            if (size < 24 || block[8] < 6) return false;
            const uint32_t fields = block[8];
            const size_t end = (24 + (fields >= 7 ? 2 : 0) + (fields > 7 ? fields - 7 : 0) + 3) & ~size_t(3);
            if (!range(end, 1, size)) return false;
            const uint8_t* rule = nullptr;
            for (uint32_t i = 0; i < count; ++i) {
                const auto* candidate = rules.data() + 32 + i * 64;
                if (!std::memcmp(candidate, block + 12, 4)) { rule = candidate; break; }
            }
            size_t cursor = end + 1;
            std::array<std::span<const uint8_t>, 2> refs;
            bool neutral = rule && (rule[12] & 2);
            for (auto& ref : refs) {
                if (!range(cursor, 2, size)) return false;
                const size_t length = block[cursor + 1];
                if (!range(cursor, 2 + length, size)) return false;
                ref = {block + cursor, 2 + length};
                for (const char* name : textures)
                    if (std::strlen(name) == length && !std::memcmp(name, block + cursor + 2, length)) neutral = true;
                cursor += 2 + length;
            }
            if (!range(cursor, block[end] >= 4 ? 2 : 1, size)) return false;
            output.insert(output.end(), block, block + end + 1);
            if (rule && (rule[12] & 1)) std::memcpy(output.data() + start + 16, rule + 4, 8);
            if (neutral && fields >= 7) put16(output.data() + start + 24, 0);
            if (neutral) output[start + end] = 11;
            for (size_t i = 0; i < refs.size(); ++i) {
                if (i == 0 && rule && (rule[12] & 2)) {
                    const char* name = reinterpret_cast<const char*>(rule + 16);
                    const size_t length = std::strlen(name);
                    output.push_back(refs[i][0]); output.push_back(static_cast<uint8_t>(length));
                    output.insert(output.end(), name, name + length);
                } else output.insert(output.end(), refs[i].begin(), refs[i].end());
            }
            if (neutral) {
                output.push_back(block[cursor]);
                output.push_back(block[end] >= 4 ? block[cursor + 1] : 0);
                output.push_back(5);
                output.insert(output.end(), 4, 0);
                output.insert(output.end(), 20, 255);
            } else output.insert(output.end(), block + cursor, block + size);
            output.resize((output.size() + 3) & ~size_t(3), 0);
            put32(output.data() + start + 4, static_cast<uint32_t>(output.size() - start));
        } else output.insert(output.end(), block, block + size);
        at += size;
        if (!std::memcmp(block, "EXT1", 4)) {
            output.insert(output.end(), original.begin() + at, original.end());
            put32(output.data() + 8, static_cast<uint32_t>(output.size()));
            return true;
        }
    }
    return false;
}

bool pc_ui_compose_camera(std::span<const uint8_t> original, std::span<const uint8_t> prompt,
                          std::vector<uint8_t>& output) {
    output.clear();
    // CAMERA_SELF is a 128x32 CI8 texture. Crop its right-hand 72px eye;
    // the GameCube C-stick artwork on the left is replaced by the Xelu R3 prompt.
    if (original.size() < 32 || original.size() > 16384 || prompt.size() != 32 + 128 * 32 * 2 || original[0] != 9 ||
        readBE16(original.data() + 2) != 128 || readBE16(original.data() + 4) != 32 ||
        original[9] > 2 || prompt[0] != 5 || readBE16(prompt.data() + 2) != 128 ||
        readBE16(prompt.data() + 4) != 32 || readBE32(prompt.data() + 28) != 32) return false;
    const uint32_t image = readBE32(original.data() + 28), palette = readBE32(original.data() + 12);
    const uint16_t colors = readBE16(original.data() + 10);
    if (!colors || colors > 256 || image < 32 || palette < 32 ||
        !range(image, 128 * 32, original.size()) || !range(palette, colors * 2, original.size())) return false;
    std::array<std::array<uint8_t, 4>, 72 * 32> eye{};
    int top = 32, bottom = 0;
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 72; ++x) {
        const int sx = x + 56;
        const uint8_t index = original[image + ((y / 4) * 16 + sx / 8) * 32 + (y % 4) * 8 + sx % 8];
        if (index >= colors) return false;
        const uint16_t value = readBE16(original.data() + palette + index * 2);
        auto& p = eye[y * 72 + x];
        if (original[9] == 0) p = {uint8_t(value), uint8_t(value), uint8_t(value), uint8_t(value >> 8)};
        else if (original[9] == 1) p = {uint8_t(((value >> 11) & 31) * 255 / 31),
            uint8_t(((value >> 5) & 63) * 255 / 63), uint8_t((value & 31) * 255 / 31), 255};
        else if (value & 0x8000) p = {uint8_t(((value >> 10) & 31) * 255 / 31),
            uint8_t(((value >> 5) & 31) * 255 / 31), uint8_t((value & 31) * 255 / 31), 255};
        else p = {uint8_t(((value >> 8) & 15) * 17), uint8_t(((value >> 4) & 15) * 17),
            uint8_t((value & 15) * 17), uint8_t(((value >> 12) & 7) * 255 / 7)};
        if (p[3]) { top = std::min(top, y); bottom = std::max(bottom, y + 1); }
    }
    if (top == 32) return false;
    output.assign(prompt.begin(), prompt.end());
    for (int y = top; y < bottom; ++y) for (int x = 0; x < 72; ++x) {
        auto p = eye[y * 72 + x];
        for (int c = 0; c < 3; ++c) p[c] = uint8_t(64 + (p[c] * 191 + 127) / 255);
        const uint16_t value = p[3] == 255 ? uint16_t(0x8000 | (p[0] * 31 / 255) << 10 |
            (p[1] * 31 / 255) << 5 | p[2] * 31 / 255) : uint16_t((p[3] * 7 / 255) << 12 |
            (p[0] / 17) << 8 | (p[1] / 17) << 4 | p[2] / 17);
        const int dx = x + 56, dy = y - top + (32 - (bottom - top)) / 2;
        put16(output.data() + 32 + ((dy / 4) * 32 + dx / 4) * 32 + ((dy % 4) * 4 + dx % 4) * 2, value);
    }
    return true;
}
