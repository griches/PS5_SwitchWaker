#pragma once

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>

// Effects drawn after the game's own frame (the Wind Waker HD look).
//
// Bloom: once a frame, on the finished scene before the HUD, what is brighter than a threshold is blurred at a quarter of the
// size and added back. Off unless DOL_AURORA_BLOOM=1 or aurora_set_bloom(true);
// DOL_AURORA_BLOOM_THRESHOLD (0 to 1, of the brightness, default 0.72) and
// DOL_AURORA_BLOOM_STRENGTH (default 0.3) are read at startup.
//
// Far blur: before bloom, depth-aware scene samples are blended by distance,
// from DOL_AURORA_FAR_BLUR_START to
// _END (distances in near-plane units, default 8000 and 30000) up to
// DOL_AURORA_FAR_BLUR_AMOUNT (default 0.4); where the game drew without depth
// (the sky, the horizon, the islands' far models), _BACKGROUND (default 0.35). Off unless
// DOL_AURORA_FAR_BLUR=1 or aurora_set_far_blur(true); _DEBUG=1 shows the
// distance as brightness.
//
// Cloud shadows: soft drifting shadows on the sea and the land, from the
// camera the game passes in (aurora_set_world_view) and the scene's depth.
// DOL_AURORA_CLOUD_SHADOWS_STRENGTH (default 0.15) and _SCALE (world units
// across a cloud, default 2200). Off unless aurora_set_cloud_shadows(true).
//
// Edge light: a warm rim on the sunlit outline of what is near the camera,
// toward the same sun. DOL_AURORA_EDGE_LIGHT_STRENGTH (default 0.4). Off unless
// aurora_set_edge_light(true).
//
// Torch light: the game's point lights (aurora_set_point_lights: up to 16 of
// x, y, z, radius, r, g, b, flicker phase) glow on the surfaces around them.
// DOL_AURORA_TORCH_LIGHT_STRENGTH (default 0.35) and _REACH (the glow's radius,
// times the game's lighting radius, default 3). Off unless
// aurora_set_torch_light(true).
namespace aurora::gfx::post_fx {
void initialize();
void shutdown();
bool ssao_enabled() noexcept;
void set_ssao(int level) noexcept;
void set_colour_grade(int preset) noexcept;
void set_image_filters(bool fxaa, int sharpening) noexcept;
bool bloom_enabled() noexcept;
void set_bloom(bool enabled) noexcept;
bool far_blur_enabled() noexcept;
void set_far_blur(bool enabled) noexcept;
bool torch_light_enabled() noexcept;
void set_torch_light(bool enabled) noexcept;
void set_point_lights(const float* lights, uint32_t count) noexcept;
bool edge_light_enabled() noexcept;
void set_edge_light(bool enabled) noexcept;
void set_sun_direction(float x, float y, float z) noexcept;
void set_cloud_weather(float x, float z, float coverage, float sunlight) noexcept;
bool cloud_shadows_enabled() noexcept;
void set_cloud_shadows(bool enabled) noexcept;
void set_world_view(const float invView[12], float tanHalfFovy, float aspect, float nearPlane, float seaLevel,
                    float seconds, float farPlane) noexcept;
bool any_enabled() noexcept;
// The game's point lights, at most (aurora_set_point_lights).
constexpr uint32_t kMaxLights = 16;
// What the effects drawn from the camera (cloud shadows, edge and
// torch light) are drawn with: the inverse view matrix's three rows, then
// tan(fovy / 2), aspect, near, the sea's height and the time in seconds, then
// the direction to the sun; and the point lights.
struct Camera {
  std::array<float, 20> view{};
  std::array<float, 4> cloudWeather{}; // wind offset X/Z, coverage, sunlight
  std::array<float, 4> depthConversion{1.f, 0.f, 0.f, 0.f};
  bool valid = false;
  std::array<float, kMaxLights * 8> lights{};
  uint32_t lightCount = 0;
};
// The camera the game set last (aurora_set_world_view and the rest).
Camera current_camera() noexcept;
// Native renderer: immutable settings/camera snapshot, recorded with the scene
// before handing a pass to the worker. Never read a later frame's live flags.
struct State {
  Camera camera;
  // World draw scissor in EFB pixels; retained across worker/interpolated frames.
  std::array<uint32_t, 4> worldScissor{0, 0, UINT32_MAX, UINT32_MAX};
  float contentScale = 1.f; // active top-left EFB region, captured on the game thread
  bool bloom = false, farBlur = false, clouds = false;
  bool edge = false, torch = false;
  int ssao = 0; // 0 off, 1 subtle, 2 strong
  int colourGrade = 0; // 0 original, 1 vivid, 2 warm
  bool fxaa = false;
  int sharpening = 0; // 0 off, 1 low, 2 high
  bool world() const { return bloom || farBlur || clouds || edge || torch || ssao; }
  bool image() const { return colourGrade || fxaa || sharpening; }
  bool any() const { return bloom || farBlur || clouds || edge || torch || ssao || colourGrade || fxaa || sharpening; }
};
State snapshot() noexcept;
// sceneTexture is the single-sample color source; contentScale selects its active
// top-left region before native dynamic-resolution upscaling.
void encode(const wgpu::CommandEncoder& cmd, const wgpu::Texture& sceneTexture,
            const wgpu::TextureView& scene, const wgpu::TextureView& depthView,
            const wgpu::Extent3D& size, wgpu::TextureFormat format, const wgpu::TextureView& dst,
            const wgpu::TextureView& resolve, uint32_t msaaSamples, const State& state);
void encode_image_filters(const wgpu::CommandEncoder& cmd, const wgpu::Texture& sceneTexture,
    const wgpu::Extent3D& size, wgpu::TextureFormat format, const wgpu::TextureView& dst,
    const wgpu::TextureView& resolve, uint32_t samples, const State& state);
// scene: the EFB as a texture (resolved when multisampled), size: its size;
// dst: the EFB's color attachment, with msaaSamples samples, and resolve its
// single-sample copy when multisampled (null otherwise).
void encode_bloom(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& scene, const wgpu::Extent3D& size,
                  wgpu::TextureFormat format, const wgpu::TextureView& dst, const wgpu::TextureView& resolve,
                  uint32_t msaaSamples, const std::array<uint32_t, 4>& scissor);
} // namespace aurora::gfx::post_fx
