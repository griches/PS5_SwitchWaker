#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aurora::gfx::post_fx {
struct ActiveRegion {
  uint32_t width, height;
  bool cropped;
};
// Match the native content-scale rounding, never creating an empty or
// out-of-bounds texture copy. Non-finite input means the full scene.
inline ActiveRegion active_region(uint32_t width, uint32_t height, float scale) {
  scale = std::isfinite(scale) ? std::clamp(scale,0.25f,1.f) : 1.f;
  const auto dimension = [scale](uint32_t size) {
    if (!size) return 0u;
    return std::min(size,std::max(1u,uint32_t(std::lround(float(size)*scale))));
  };
  const auto w = dimension(width), h = dimension(height);
  return {w,h,w != width || h != height};
}
} // namespace aurora::gfx::post_fx
