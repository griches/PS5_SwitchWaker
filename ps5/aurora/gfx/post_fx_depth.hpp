#pragma once
#include <array>
#include <cmath>
namespace aurora::gfx::post_fx {
// Native reversed Z: d = n*f/((f-n)*distance) - n/(f-n).
// Effects expect n/distance, so convert once before any depth-based pass.
inline std::array<float,4> depth_conversion(float nearPlane, float farPlane) {
    if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0 || farPlane <= nearPlane)
        return {1.f,0.f,0.f,0.f};
    return {(farPlane-nearPlane)/farPlane,nearPlane/farPlane,0.f,0.f};
}
} // namespace aurora::gfx::post_fx
