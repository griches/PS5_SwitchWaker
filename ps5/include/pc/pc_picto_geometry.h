// Picto Box geometry. Gallery mapping and crop derived from BlueWake's
// scripts/mods/widescreen_aspect.py and RecompCore patch 0180 (pdale-boop).
#pragma once
#include <cmath>

namespace pc::picto {
struct Viewport { float x, width; };
inline Viewport gallery(float left, float right) {
    const float scale = 640.0f / (right - left);
    return {(-9.0f - left) * scale, 659.0f * scale};
}
inline float fraction(float aspect) { return (4.0f / 3.0f) / aspect; }
// Keep the native viewfinder's margins, but map them through the same
// camera-centred narrowing as the photo copy (4:3 remains -9..650).
inline Viewport viewfinder(float aspect) {
    const float width = 659.0f / fraction(aspect);
    return {320.5f - width * 0.5f, width};
}
// Camera widening is about the EFB centre, not the off-centre shutter's centre.
inline int sourceX(int x, float aspect) {
    return static_cast<int>(std::lround(320.0f + (x - 320.0f) * fraction(aspect)));
}
struct Span { int left, width; };
inline Span sourceSpan(int left, int width, float aspect) {
    return {sourceX(left, aspect), static_cast<int>(std::lround(width * fraction(aspect)))};
}
inline bool actorSlot(unsigned argb, int count) {
    const unsigned slot = argb >> 26;
    return slot < 63 && slot < static_cast<unsigned>(count);
}
// Keep the game's pixel-count thresholds in original-camera units. Ratios
// are calculated in actual EFB pixels before this conversion.
inline int originalPixels(int pixels, float aspect) {
    return static_cast<int>(std::lround(pixels / fraction(aspect)));
}
} // namespace pc::picto
