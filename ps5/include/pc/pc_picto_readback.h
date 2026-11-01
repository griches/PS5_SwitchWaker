// Bounds-checked sampling of the current photograph's deferred RGBA snapshot.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pc::picto {
inline uint32_t peekArgb(bool valid, const std::vector<uint8_t>& rgba,
                         uint32_t srcWidth, uint32_t srcHeight,
                         uint16_t left, uint16_t top, uint16_t width, uint16_t height,
                         uint16_t x, uint16_t y) {
    if (!valid || !srcWidth || !srcHeight || !width || !height ||
        x < left || y < top || uint32_t(x) >= uint32_t(left) + width ||
        uint32_t(y) >= uint32_t(top) + height ||
        (rgba.size() % 4) != 0 || uint64_t(srcWidth) * srcHeight != rgba.size() / 4) {
        return 0xFFFFFFFF; // clear alpha: never credit a failed/stale read to a subject
    }
    const uint32_t sx = (uint64_t(x - left) * 2 + 1) * srcWidth / (2u * width);
    const uint32_t sy = (uint64_t(y - top) * 2 + 1) * srcHeight / (2u * height);
    const uint8_t* p = rgba.data() + (size_t(sy) * srcWidth + sx) * 4;
    return uint32_t(p[3]) << 24 | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
}
} // namespace pc::picto
