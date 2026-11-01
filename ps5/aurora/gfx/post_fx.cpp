// Adapted from Chris Sotraidis's BlueWake/RecompCore Aurora patches
// Bloom, far blur, clouds, edge and torch lighting; native recording/encoding adapter below.
#include "post_fx.hpp"
#include "post_fx_shaders.hpp"
#include "post_fx_region.hpp"
#include "post_fx_depth.hpp"

#include <aurora/aurora.h>

#include "internal.hpp"
#include "webgpu/gpu.hpp"
#include "webgpu/gpu_prof.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace aurora::gfx::post_fx {
using webgpu::g_device;

namespace {
std::atomic_int g_ssao{0};
std::atomic_int g_colourGrade{0};
std::atomic_int g_imageFilters{0}; // bit 0 FXAA, remaining bits sharpening
wgpu::ShaderModule g_filterModule;
wgpu::BindGroupLayout g_filterLayout;
wgpu::PipelineLayout g_filterPipelineLayout;
std::array<std::array<wgpu::RenderPipeline, 2>, 2> g_filterPipelines;
wgpu::ShaderModule g_gradeModule;
wgpu::Texture g_imageWork;
wgpu::TextureView g_imageWorkView;
wgpu::Texture g_imageSource;
wgpu::TextureView g_imageSourceView;
wgpu::Extent3D g_imageSize{};
wgpu::TextureFormat g_imageFormat = wgpu::TextureFormat::Undefined;
std::array<wgpu::RenderPipeline, 2> g_gradePipelines;
uint32_t g_imageSamples = 0;
wgpu::ShaderModule g_ssaoModule;
wgpu::RenderPipeline g_ssaoPass;
uint32_t g_ssaoSamples = 0;
std::atomic_bool g_bloom{false};
std::atomic_bool g_farBlur{false};
std::atomic_bool g_clouds{false};
std::atomic_bool g_edge{false};
std::atomic_bool g_torch{false};
float g_torchStrength = 0.35f;  // how bright a light's glow is
float g_torchRadiusScale = 3.f; // the glow's reach, times the radius the game lights with
// The game's point lights (aurora_set_point_lights): x, y, z, radius, r, g, b, flicker phase (zero disables it).
std::array<float, 4> g_depthConversion{1.f,0.f,0.f,0.f};
std::array<float, kMaxLights * 8> g_lights{};
uint32_t g_lightCount = 0;
float g_edgeStrength = 0.4f;  // how bright a sunlit edge gets
float g_cloudStrength = 0.15f;  // how dark a cloud's shadow is
float g_cloudScale = 2200.f;    // local patches visible while walking, not island-wide dimming
bool g_cloudDebug = false;
// The camera, for the cloud shadows (aurora_set_world_view): the inverse view
// matrix's three rows, then tan(fovy / 2), aspect, near, the sea's height, and
// the time in seconds, then the direction to the sun (aurora_set_sun_direction).
// Written by the game thread, read when a frame is encoded.
std::mutex g_cameraMutex;
std::array<float, 20> g_camera{};
std::array<float, 4> g_cloudWeather{};
bool g_cameraValid = false;
float g_farStart = 8000.f;  // distance in near-plane units where the far blur starts
float g_farEnd = 30000.f;   // and where it is full
float g_farAmount = 0.4f;  // how much of the blurred scene at full
float g_farBackground = 0.35f;  // and where there is no depth (the sky, the horizon)
bool g_farDebug = false;
float g_threshold = 0.72f;
float g_strength = 0.3f;

wgpu::ShaderModule g_module;
wgpu::BindGroupLayout g_layout;
wgpu::PipelineLayout g_pipelineLayout;
wgpu::Sampler g_sampler;
wgpu::TextureFormat g_format = wgpu::TextureFormat::Undefined;
wgpu::RenderPipeline g_bright, g_blurH, g_blurV;
wgpu::RenderPipeline g_composite;
wgpu::Texture g_farSource;
wgpu::TextureView g_farSourceView;
wgpu::Extent3D g_farSourceSize{};
wgpu::TextureFormat g_farSourceFormat = wgpu::TextureFormat::Undefined;
wgpu::RenderPipeline g_farComposite;   // blended into the EFB by distance
uint32_t g_farSamples = 0;
wgpu::BindGroupLayout g_depthLayout;
wgpu::PipelineLayout g_depthPipelineLayout;
uint32_t g_compositeSamples = 0;
std::array<wgpu::Texture, 2> g_tex;
std::array<wgpu::TextureView, 2> g_view;
wgpu::Extent3D g_size{};

// Worker-owned resolve for depth-based effects. Reversed Z uses the nearest
// covered sample (maximum depth); uncovered background samples are zero.
wgpu::Texture g_resolvedDepth;
wgpu::TextureView g_resolvedDepthView;
wgpu::Extent3D g_resolvedDepthSize{};
std::array<wgpu::BindGroupLayout,2> g_resolveDepthLayouts;
std::array<wgpu::RenderPipeline,2> g_resolveDepthPipelines;
wgpu::Texture g_croppedScene;
wgpu::TextureView g_croppedSceneView;
wgpu::Extent3D g_croppedSceneSize{};
wgpu::TextureFormat g_croppedSceneFormat = wgpu::TextureFormat::Undefined;
wgpu::RenderPipeline g_cropComposite;
uint32_t g_cropCompositeSamples = 0;
using shaders::kResolveDepthShader;

wgpu::TextureView resolve_effect_depth(const wgpu::CommandEncoder& cmd,
                                      const wgpu::TextureView& source,
                                      const wgpu::Extent3D& size, uint32_t samples, const Camera& camera) {
  if (!source) return source;
  const unsigned variant = samples > 1 ? 1 : 0;
  auto& pipeline = g_resolveDepthPipelines[variant];
  auto& bindLayout = g_resolveDepthLayouts[variant];
  if (!pipeline) {
    const std::array entries{wgpu::BindGroupLayoutEntry{
        .binding = 0,
        .visibility = wgpu::ShaderStage::Fragment,
        .texture = {.sampleType = wgpu::TextureSampleType::Depth,
                    .viewDimension = wgpu::TextureViewDimension::e2D,
                    .multisampled = samples > 1},
    }, wgpu::BindGroupLayoutEntry{
        .binding = 1, .visibility = wgpu::ShaderStage::Fragment,
        .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = 16}}};
    const wgpu::BindGroupLayoutDescriptor layoutDesc{.entryCount = entries.size(), .entries = entries.data()};
    bindLayout = g_device.CreateBindGroupLayout(&layoutDesc);
    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{
        .bindGroupLayoutCount = 1, .bindGroupLayouts = &bindLayout};
    const auto layout = g_device.CreatePipelineLayout(&pipelineLayoutDesc);
    wgpu::ShaderSourceWGSL code{};
    const std::string shaderSource = std::string(samples > 1 ? R"(
@group(0) @binding(0) var source: texture_depth_multisampled_2d;
fn load_depth(pixel: vec2i) -> f32 {
    var nearest = 0.0;
    for (var i = 0u; i < textureNumSamples(source); i++) {
        nearest = max(nearest,textureLoad(source,pixel,i32(i)));
    }
    return nearest;
}
)" : R"(
@group(0) @binding(0) var source: texture_depth_2d;
fn load_depth(pixel: vec2i) -> f32 { return textureLoad(source,pixel,0); }
)") + kResolveDepthShader;
    code.code = shaderSource.c_str();
    const wgpu::ShaderModuleDescriptor shaderDesc{.nextInChain = &code, .label = "Effect depth resolve"};
    const auto shader = g_device.CreateShaderModule(&shaderDesc);
    const wgpu::DepthStencilState depth{
        .format = wgpu::TextureFormat::Depth32Float,
        .depthWriteEnabled = true,
        .depthCompare = wgpu::CompareFunction::Always};
    const wgpu::FragmentState fragment{.module = shader, .entryPoint = "fs_main"};
    const wgpu::RenderPipelineDescriptor descriptor{
        .label = "Effect depth resolve",
        .layout = layout,
        .vertex = {.module = shader, .entryPoint = "vs_main"},
        .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
        .depthStencil = &depth,
        .fragment = &fragment};
    pipeline = g_device.CreateRenderPipeline(&descriptor);
  }
  if (!g_resolvedDepth || g_resolvedDepthSize.width != size.width || g_resolvedDepthSize.height != size.height) {
    const wgpu::TextureDescriptor descriptor{
        .label = "Effect resolved depth",
        .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding,
        .dimension = wgpu::TextureDimension::e2D,
        .size = size,
        .format = wgpu::TextureFormat::Depth32Float};
    g_resolvedDepth = g_device.CreateTexture(&descriptor);
    g_resolvedDepthView = g_resolvedDepth.CreateView();
    g_resolvedDepthSize = size;
  }
  const wgpu::BufferDescriptor conversionDesc{.label = "Effect depth conversion",
      .usage = wgpu::BufferUsage::Uniform, .size = 16, .mappedAtCreation = true};
  const auto conversion = g_device.CreateBuffer(&conversionDesc);
  std::memcpy(conversion.GetMappedRange(),camera.depthConversion.data(),16);
  conversion.Unmap();
  const std::array entries{wgpu::BindGroupEntry{.binding = 0, .textureView = source},
      wgpu::BindGroupEntry{.binding = 1, .buffer = conversion, .size = 16}};
  const wgpu::BindGroupDescriptor bindDesc{.layout = bindLayout, .entryCount = entries.size(), .entries = entries.data()};
  const auto bind = g_device.CreateBindGroup(&bindDesc);
  const wgpu::RenderPassDepthStencilAttachment depth{
      .view = g_resolvedDepthView,
      .depthLoadOp = wgpu::LoadOp::Clear,
      .depthStoreOp = wgpu::StoreOp::Store,
      .depthClearValue = 0};
  const wgpu::RenderPassDescriptor descriptor{.label = "Effect depth resolve", .depthStencilAttachment = &depth};
  auto pass = cmd.BeginRenderPass(&descriptor);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0,bind);
  pass.Draw(3);
  pass.End();
  return g_resolvedDepthView;
}


using shaders::kShader;

// Depth-aware far blur: nearby depth layers remain separate, blended
// back by distance. Aurora's depth is reversed (1 near, 0 far) and, for a
// perspective view, near / distance: 1 / depth is the distance in near-plane
// units. Wind Waker draws the sky, the sea toward the horizon and the islands'
// far models without depth (0): those get the background amount.
using shaders::kFarShader;
wgpu::ShaderModule g_farModule;

// Cloud shadows on the sea and the islands: each pixel's place in the world
// (from depth, or where the view ray meets the sea where there is no depth),
// darkened by procedural patches driven by game wind, cloud coverage and sunlight.
using shaders::kCloudShader;
wgpu::ShaderModule g_cloudModule;

using shaders::kEdgeShader;
wgpu::ShaderModule g_edgeModule;

// Torch and fire light: the game's own point lights (torches, fires, lanterns,
// bombs) glow on the surfaces around them, each pixel's place in the world from
// the camera and the depth, with a smooth falloff to the light's radius.
using shaders::kTorchShader;
wgpu::ShaderModule g_torchModule;
wgpu::BindGroupLayout g_torchLayout;
wgpu::PipelineLayout g_torchPipelineLayout;
wgpu::RenderPipeline g_torchPass;
uint32_t g_torchSamples = 0;
constexpr size_t kTorchUniformSize = 20 * sizeof(float) + 8 * sizeof(float) + kMaxLights * 8 * sizeof(float);
wgpu::RenderPipeline g_edgePass;
uint32_t g_edgeSamples = 0;
wgpu::BindGroupLayout g_cloudLayout;
wgpu::PipelineLayout g_cloudPipelineLayout;
wgpu::RenderPipeline g_cloudPass;
uint32_t g_cloudSamples = 0;

float env_float(const char* name, float fallback, float lo, float hi) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const float parsed = std::strtof(value, &end);
  if (end == value || *end != '\0' || !std::isfinite(parsed) || parsed < lo || parsed > hi) {
    return fallback;
  }
  return parsed;
}

enum Blend { kReplace, kAdd };

wgpu::RenderPipeline make_pipeline(const char* label, const char* entry, wgpu::TextureFormat format,
                                   uint32_t samples, Blend mode, const wgpu::ShaderModule& module = nullptr,
                                   const wgpu::PipelineLayout& layout = nullptr) {
  const std::array constants{
      wgpu::ConstantEntry{.key = "threshold", .value = g_threshold},
      wgpu::ConstantEntry{.key = "strength", .value = g_strength},
  };
  const wgpu::BlendState add{
      .color = {.operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::One,
                .dstFactor = wgpu::BlendFactor::One},
      .alpha = {.operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::Zero,
                .dstFactor = wgpu::BlendFactor::One},
  };
  const std::array targets{wgpu::ColorTargetState{
      .format = format,
      .blend = mode == kAdd ? &add : nullptr,
      .writeMask = wgpu::ColorWriteMask::All,
  }};
  const wgpu::FragmentState fragment{
      .module = module ? module : g_module,
      .entryPoint = entry,
      .constantCount = module ? 0 : constants.size(),
      .constants = module ? nullptr : constants.data(),
      .targetCount = targets.size(),
      .targets = targets.data(),
  };
  const wgpu::RenderPipelineDescriptor descriptor{
      .label = label,
      .layout = layout ? layout : g_pipelineLayout,
      .vertex = {.module = module ? module : g_module, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .multisample = {.count = samples, .mask = UINT32_MAX},
      .fragment = &fragment,
  };
  return g_device.CreateRenderPipeline(&descriptor);
}


wgpu::RenderPipeline make_cloud_pipeline(wgpu::TextureFormat format, uint32_t samples) {
  const std::array constants{
      wgpu::ConstantEntry{.key = "strength", .value = g_cloudStrength},
      wgpu::ConstantEntry{.key = "scale", .value = g_cloudScale},
      wgpu::ConstantEntry{.key = "debug_view", .value = g_cloudDebug ? 1.0 : 0.0},
  };
  const wgpu::BlendState multiply{
      .color = {.operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::Zero,
                .dstFactor = wgpu::BlendFactor::Src},
      .alpha = {.operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::Zero,
                .dstFactor = wgpu::BlendFactor::One},
  };
  const std::array targets{wgpu::ColorTargetState{.format = format, .blend = &multiply, .writeMask = wgpu::ColorWriteMask::All}};
  const wgpu::FragmentState fragment{
      .module = g_cloudModule,
      .entryPoint = "fs_clouds",
      .constantCount = constants.size(),
      .constants = constants.data(),
      .targetCount = targets.size(),
      .targets = targets.data(),
  };
  const wgpu::RenderPipelineDescriptor descriptor{
      .label = "Cloud shadows",
      .layout = g_cloudPipelineLayout,
      .vertex = {.module = g_cloudModule, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .multisample = {.count = samples, .mask = UINT32_MAX},
      .fragment = &fragment,
  };
  return g_device.CreateRenderPipeline(&descriptor);
}


wgpu::RenderPipeline make_ssao_pipeline(wgpu::TextureFormat format, uint32_t samples) {
  const wgpu::BlendState multiply{
      .color = {.operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::Zero,
                .dstFactor = wgpu::BlendFactor::Src},
      .alpha = {.operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::Zero,
                .dstFactor = wgpu::BlendFactor::One},
  };
  const std::array targets{wgpu::ColorTargetState{.format = format, .blend = &multiply, .writeMask = wgpu::ColorWriteMask::All}};
  const wgpu::FragmentState fragment{
      .module = g_ssaoModule,
      .entryPoint = "fs_ssao",
      .targetCount = targets.size(),
      .targets = targets.data(),
  };
  const wgpu::RenderPipelineDescriptor descriptor{
      .label = "Ambient occlusion",
      .layout = g_cloudPipelineLayout,
      .vertex = {.module = g_ssaoModule, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .multisample = {.count = samples, .mask = UINT32_MAX},
      .fragment = &fragment,
  };
  return g_device.CreateRenderPipeline(&descriptor);
}


// direct: multiplied into the scene (the EFB's alpha kept), else written to a target of its own.

wgpu::RenderPipeline make_edge_pipeline(wgpu::TextureFormat format, uint32_t samples) {
  const std::array constants{wgpu::ConstantEntry{.key = "strength", .value = g_edgeStrength}};
  const wgpu::BlendState add{
      .color = {.operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::One,
                .dstFactor = wgpu::BlendFactor::One},
      .alpha = {.operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::Zero,
                .dstFactor = wgpu::BlendFactor::One},
  };
  const std::array targets{wgpu::ColorTargetState{.format = format, .blend = &add, .writeMask = wgpu::ColorWriteMask::All}};
  const wgpu::FragmentState fragment{
      .module = g_edgeModule,
      .entryPoint = "fs_edge",
      .constantCount = constants.size(),
      .constants = constants.data(),
      .targetCount = targets.size(),
      .targets = targets.data(),
  };
  const wgpu::RenderPipelineDescriptor descriptor{
      .label = "Edge light",
      .layout = g_cloudPipelineLayout,
      .vertex = {.module = g_edgeModule, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .multisample = {.count = samples, .mask = UINT32_MAX},
      .fragment = &fragment,
  };
  return g_device.CreateRenderPipeline(&descriptor);
}

wgpu::RenderPipeline make_torch_pipeline(wgpu::TextureFormat format, uint32_t samples) {
  const std::array constants{
      wgpu::ConstantEntry{.key = "strength", .value = g_torchStrength},
      wgpu::ConstantEntry{.key = "radius_scale", .value = g_torchRadiusScale},
  };
  const wgpu::BlendState add{
      .color = {.operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::One,
                .dstFactor = wgpu::BlendFactor::One},
      .alpha = {.operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::Zero,
                .dstFactor = wgpu::BlendFactor::One},
  };
  const std::array targets{wgpu::ColorTargetState{.format = format, .blend = &add, .writeMask = wgpu::ColorWriteMask::All}};
  const wgpu::FragmentState fragment{
      .module = g_torchModule,
      .entryPoint = "fs_torch",
      .constantCount = constants.size(),
      .constants = constants.data(),
      .targetCount = targets.size(),
      .targets = targets.data(),
  };
  const wgpu::RenderPipelineDescriptor descriptor{
      .label = "Torch light",
      .layout = g_torchPipelineLayout,
      .vertex = {.module = g_torchModule, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .multisample = {.count = samples, .mask = UINT32_MAX},
      .fragment = &fragment,
  };
  return g_device.CreateRenderPipeline(&descriptor);
}

wgpu::RenderPipeline make_far_pipeline(wgpu::TextureFormat format, uint32_t samples) {
  const std::array constants{
      wgpu::ConstantEntry{.key = "far_start", .value = g_farStart},
      wgpu::ConstantEntry{.key = "far_end", .value = g_farEnd},
      wgpu::ConstantEntry{.key = "amount", .value = g_farAmount},
      wgpu::ConstantEntry{.key = "background", .value = g_farBackground},
      wgpu::ConstantEntry{.key = "debug_view", .value = g_farDebug ? 1.0 : 0.0},
  };
  const wgpu::BlendState over{
      .color = {.operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::SrcAlpha,
                .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha},
      .alpha = {.operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::Zero,
                .dstFactor = wgpu::BlendFactor::One},
  };
  const std::array targets{wgpu::ColorTargetState{.format = format, .blend = &over, .writeMask = wgpu::ColorWriteMask::All}};
  const wgpu::FragmentState fragment{
      .module = g_farModule,
      .entryPoint = "fs_far",
      .constantCount = constants.size(),
      .constants = constants.data(),
      .targetCount = targets.size(),
      .targets = targets.data(),
  };
  const wgpu::RenderPipelineDescriptor descriptor{
      .label = "Far blur composite",
      .layout = g_depthPipelineLayout,
      .vertex = {.module = g_farModule, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .multisample = {.count = samples, .mask = UINT32_MAX},
      .fragment = &fragment,
  };
  return g_device.CreateRenderPipeline(&descriptor);
}

void ensure_targets(const wgpu::Extent3D& sceneSize, wgpu::TextureFormat format, uint32_t samples,
                    const State& state) {
  if (format != g_format) {
    g_format = format;
    g_bright = {}; g_blurH = {}; g_blurV = {};
    g_cloudPass = {}; g_edgePass = {}; g_torchPass = {}; g_ssaoPass = {};
    g_composite = {}; g_farComposite = {};
    g_size = {};
  }
  if (state.bloom) {
    if (!g_blurH) g_blurH = make_pipeline("Bloom blur (horizontal)", "fs_blur_h", format, 1, kReplace);
    if (!g_blurV) g_blurV = make_pipeline("Bloom blur (vertical)", "fs_blur_v", format, 1, kReplace);
  }
  if (state.bloom) {
    if (!g_bright) g_bright = make_pipeline("Bloom bright pass", "fs_bright", format, 1, kReplace);
    if (!g_composite || samples != g_compositeSamples) {
      g_composite = make_pipeline("Bloom composite", "fs_composite", format, samples, kAdd);
      g_compositeSamples = samples;
    }
  }
  if (state.ssao && (!g_ssaoPass || samples != g_ssaoSamples)) {
    if (!g_ssaoModule) {
      wgpu::ShaderSourceWGSL source{};
      source.code = shaders::kSsaoShader;
      const wgpu::ShaderModuleDescriptor descriptor{.nextInChain = &source, .label = "Ambient occlusion"};
      g_ssaoModule = g_device.CreateShaderModule(&descriptor);
    }
    g_ssaoPass = make_ssao_pipeline(format, samples);
    g_ssaoSamples = samples;
  }
  if (state.torch && (!g_torchPass || samples != g_torchSamples)) {
    g_torchPass = make_torch_pipeline(format, samples);
    g_torchSamples = samples;
  }
  if (state.edge && (!g_edgePass || samples != g_edgeSamples)) {
    g_edgePass = make_edge_pipeline(format, samples);
    g_edgeSamples = samples;
  }
  if (state.clouds && (!g_cloudPass || samples != g_cloudSamples)) {
    g_cloudPass = make_cloud_pipeline(format, samples);
    g_cloudSamples = samples;
  }
  if (state.farBlur) {
    if (!g_farComposite || samples != g_farSamples) {
      g_farComposite = make_far_pipeline(format, samples);
      g_farSamples = samples;
    }
  }
  const wgpu::Extent3D size{std::max(1u, sceneSize.width / 4), std::max(1u, sceneSize.height / 4), 1};
  if (size.width != g_size.width || size.height != g_size.height) {
    g_size = size;
    g_tex = {}; g_view = {};
  }
  const auto allocate = [&](auto& textures, auto& views, const char* label) {
    if (textures[0]) return;
    for (size_t i = 0; i < textures.size(); ++i) {
      const wgpu::TextureDescriptor descriptor{
          .label = label,
          .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding,
          .dimension = wgpu::TextureDimension::e2D,
          .size = size, .format = format, .mipLevelCount = 1, .sampleCount = 1};
      textures[i] = g_device.CreateTexture(&descriptor);
      views[i] = textures[i].CreateView();
    }
  };
  if (state.bloom) allocate(g_tex, g_view, "Bloom texture");
}

wgpu::Buffer image_uniform(const std::array<float, 8>& data) {
  const wgpu::BufferDescriptor desc{.label = "Image bounds", .usage = wgpu::BufferUsage::Uniform,
      .size = sizeof(data), .mappedAtCreation = true};
  const auto buffer = g_device.CreateBuffer(&desc);
  std::memcpy(buffer.GetMappedRange(0, sizeof(data)), data.data(), sizeof(data));
  buffer.Unmap();
  return buffer;
}
wgpu::Buffer crop_uniform(const wgpu::Extent3D& size, const std::array<uint32_t,4>& clip) {
  return image_uniform({float(clip[0])/size.width,float(clip[1])/size.height,
      float(clip[2])/size.width,float(clip[3])/size.height,0,0,0,0});
}

void draw(const wgpu::CommandEncoder& cmd, const char* label, const wgpu::RenderPipeline& pipeline,
          const wgpu::TextureView& source, const wgpu::TextureView& target, bool load,
          const wgpu::TextureView& resolve = nullptr, const wgpu::Extent3D* viewport = nullptr,
          const std::array<uint32_t, 4>* scissor = nullptr,
          const wgpu::Buffer& imageParams = nullptr) {
  const auto uniform = imageParams ? imageParams : image_uniform({0,0,1,1,0,0,0,0});
  const std::array entries{
      wgpu::BindGroupEntry{.binding = 0, .sampler = g_sampler},
      wgpu::BindGroupEntry{.binding = 1, .textureView = source},
      wgpu::BindGroupEntry{.binding = 4, .buffer = uniform, .size = 32},
  };
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .layout = g_layout,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  const std::array attachments{wgpu::RenderPassColorAttachment{
      .view = target,
      .resolveTarget = resolve,
      .loadOp = load ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear,
      .storeOp = wgpu::StoreOp::Store,
      .clearValue = {0.0, 0.0, 0.0, 0.0},
  }};
  const wgpu::RenderPassDescriptor descriptor{
      .label = label,
      .colorAttachmentCount = attachments.size(),
      .colorAttachments = attachments.data(),
      .timestampWrites = webgpu::gpu_prof::pass_writes(label),
  };
  const auto pass = cmd.BeginRenderPass(&descriptor);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup, 0, nullptr);
  if (viewport) pass.SetViewport(0,0,float(viewport->width),float(viewport->height),0,1);
  if (scissor) pass.SetScissorRect((*scissor)[0], (*scissor)[1], (*scissor)[2], (*scissor)[3]);
  pass.Draw(3);
  pass.End();
}
void draw_depth(const wgpu::CommandEncoder& cmd, const char* label, const wgpu::RenderPipeline& pipeline,
                const wgpu::TextureView& source, const wgpu::TextureView& depthView, const wgpu::TextureView& target,
                const wgpu::TextureView& resolve, bool load, const std::array<uint32_t, 4>& scissor,
                const wgpu::Buffer& bounds) {
  const std::array entries{
      wgpu::BindGroupEntry{.binding = 0, .sampler = g_sampler},
      wgpu::BindGroupEntry{.binding = 1, .textureView = source},
      wgpu::BindGroupEntry{.binding = 2, .textureView = depthView},
      wgpu::BindGroupEntry{.binding = 4, .buffer = bounds, .size = 32},
  };
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .layout = g_depthLayout,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  const std::array attachments{wgpu::RenderPassColorAttachment{
      .view = target,
      .resolveTarget = resolve,
      .loadOp = load ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear,
      .storeOp = wgpu::StoreOp::Store,
      .clearValue = {1.0, 1.0, 1.0, 1.0},
  }};
  const wgpu::RenderPassDescriptor descriptor{
      .label = label,
      .colorAttachmentCount = attachments.size(),
      .colorAttachments = attachments.data(),
      .timestampWrites = webgpu::gpu_prof::pass_writes(label),
  };
  const auto pass = cmd.BeginRenderPass(&descriptor);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup, 0, nullptr);
  pass.SetScissorRect(scissor[0], scissor[1], scissor[2], scissor[3]);
  pass.Draw(3);
  pass.End();
}
void draw_with_uniform(const wgpu::CommandEncoder& cmd, const char* label, const wgpu::RenderPipeline& pipeline,
                       const wgpu::BindGroupLayout& layout, const void* uniform, size_t uniformSize,
                       const wgpu::TextureView& depthView, const wgpu::TextureView& target,
                       const wgpu::TextureView& resolve, const std::array<uint32_t, 4>* scissor = nullptr);

void draw_with_uniform(const wgpu::CommandEncoder& cmd, const char* label, const wgpu::RenderPipeline& pipeline,
                       const wgpu::BindGroupLayout& layout, const void* uniform, size_t uniformSize,
                       const wgpu::TextureView& depthView, const wgpu::TextureView& target,
                       const wgpu::TextureView& resolve, const std::array<uint32_t, 4>* scissor) {
  // A small buffer for each frame, so a frame still in flight keeps its camera.
  const wgpu::BufferDescriptor bufferDescriptor{
      .label = "Post effects view",
      .usage = wgpu::BufferUsage::Uniform,
      .size = uniformSize,
      .mappedAtCreation = true,
  };
  const auto buffer = g_device.CreateBuffer(&bufferDescriptor);
  std::memcpy(buffer.GetMappedRange(0, uniformSize), uniform, uniformSize);
  buffer.Unmap();
  const std::array entries{
      wgpu::BindGroupEntry{.binding = 2, .textureView = depthView},
      wgpu::BindGroupEntry{.binding = 3, .buffer = buffer, .offset = 0, .size = uniformSize},
  };
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .layout = layout,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  const std::array attachments{wgpu::RenderPassColorAttachment{
      .view = target,
      .resolveTarget = resolve,
      .loadOp = wgpu::LoadOp::Load,
      .storeOp = wgpu::StoreOp::Store,
  }};
  const wgpu::RenderPassDescriptor descriptor{
      .label = label,
      .colorAttachmentCount = attachments.size(),
      .colorAttachments = attachments.data(),
      .timestampWrites = webgpu::gpu_prof::pass_writes(label),
  };
  const auto pass = cmd.BeginRenderPass(&descriptor);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup, 0, nullptr);
  if (scissor) pass.SetScissorRect((*scissor)[0], (*scissor)[1], (*scissor)[2], (*scissor)[3]);
  pass.Draw(3);
  pass.End();
}
} // namespace

void initialize() {
  const char* bloom = std::getenv("DOL_AURORA_BLOOM");
  g_bloom.store(bloom != nullptr && bloom[0] == '1', std::memory_order_relaxed);
  g_threshold = env_float("DOL_AURORA_BLOOM_THRESHOLD", g_threshold, 0.f, 1.f);
  g_strength = env_float("DOL_AURORA_BLOOM_STRENGTH", g_strength, 0.f, 4.f);
  const char* far = std::getenv("DOL_AURORA_FAR_BLUR");
  g_farBlur.store(far != nullptr && far[0] == '1', std::memory_order_relaxed);
  g_farStart = env_float("DOL_AURORA_FAR_BLUR_START", g_farStart, 1.f, 1e9f);
  g_farEnd = env_float("DOL_AURORA_FAR_BLUR_END", g_farEnd, g_farStart + 1.f, 1e9f);
  g_farAmount = env_float("DOL_AURORA_FAR_BLUR_AMOUNT", g_farAmount, 0.f, 1.f);
  g_farBackground = env_float("DOL_AURORA_FAR_BLUR_BACKGROUND", g_farBackground, 0.f, 1.f);
  g_cloudStrength = env_float("DOL_AURORA_CLOUD_SHADOWS_STRENGTH", g_cloudStrength, 0.f, 1.f);
  g_cloudScale = env_float("DOL_AURORA_CLOUD_SHADOWS_SCALE", g_cloudScale, 1.f, 1e7f);
  g_edgeStrength = env_float("DOL_AURORA_EDGE_LIGHT_STRENGTH", g_edgeStrength, 0.f, 4.f);
  g_torchStrength = env_float("DOL_AURORA_TORCH_LIGHT_STRENGTH", g_torchStrength, 0.f, 4.f);
  g_torchRadiusScale = env_float("DOL_AURORA_TORCH_LIGHT_REACH", g_torchRadiusScale, 0.1f, 50.f);
  const char* cloudDebug = std::getenv("DOL_AURORA_CLOUD_SHADOWS_DEBUG");
  g_cloudDebug = cloudDebug != nullptr && cloudDebug[0] == '1';
  const char* farDebug = std::getenv("DOL_AURORA_FAR_BLUR_DEBUG");
  g_farDebug = farDebug != nullptr && farDebug[0] == '1';

  wgpu::ShaderSourceWGSL source{};
  source.code = kShader;
  const wgpu::ShaderModuleDescriptor moduleDescriptor{.nextInChain = &source, .label = "Post effects"};
  g_module = g_device.CreateShaderModule(&moduleDescriptor);
  const std::array layoutEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment,
          .sampler = {.type = wgpu::SamplerBindingType::Filtering},
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture = {.sampleType = wgpu::TextureSampleType::Float, .viewDimension = wgpu::TextureViewDimension::e2D},
      },
      wgpu::BindGroupLayoutEntry{.binding = 4, .visibility = wgpu::ShaderStage::Fragment,
          .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = 32}},
  };
  const wgpu::BindGroupLayoutDescriptor layoutDescriptor{
      .label = "Post effects",
      .entryCount = layoutEntries.size(),
      .entries = layoutEntries.data(),
  };
  g_layout = g_device.CreateBindGroupLayout(&layoutDescriptor);
  const wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor{.bindGroupLayoutCount = 1, .bindGroupLayouts = &g_layout};
  g_pipelineLayout = g_device.CreatePipelineLayout(&pipelineLayoutDescriptor);
  const wgpu::SamplerDescriptor samplerDescriptor{
      .label = "Post effects",
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
  };
  g_sampler = g_device.CreateSampler(&samplerDescriptor);

  wgpu::ShaderSourceWGSL farSource{};
  farSource.code = kFarShader;
  const wgpu::ShaderModuleDescriptor farModuleDescriptor{.nextInChain = &farSource, .label = "Far blur"};
  g_farModule = g_device.CreateShaderModule(&farModuleDescriptor);
  wgpu::ShaderSourceWGSL cloudSource{};
  cloudSource.code = kCloudShader;
  const wgpu::ShaderModuleDescriptor cloudModuleDescriptor{.nextInChain = &cloudSource, .label = "Cloud shadows"};
  g_cloudModule = g_device.CreateShaderModule(&cloudModuleDescriptor);
  wgpu::ShaderSourceWGSL edgeSource{};
  edgeSource.code = kEdgeShader;
  const wgpu::ShaderModuleDescriptor edgeModuleDescriptor{.nextInChain = &edgeSource, .label = "Edge light"};
  g_edgeModule = g_device.CreateShaderModule(&edgeModuleDescriptor);
  wgpu::ShaderSourceWGSL torchSource{};
  torchSource.code = kTorchShader;
  const wgpu::ShaderModuleDescriptor torchModuleDescriptor{.nextInChain = &torchSource, .label = "Torch light"};
  g_torchModule = g_device.CreateShaderModule(&torchModuleDescriptor);
  const std::array depthEntries{
      layoutEntries[0],
      layoutEntries[1],
      wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture = {.sampleType = wgpu::TextureSampleType::Depth, .viewDimension = wgpu::TextureViewDimension::e2D},
      },
      layoutEntries[2],
  };
  const wgpu::BindGroupLayoutDescriptor depthLayoutDescriptor{
      .label = "Far blur",
      .entryCount = depthEntries.size(),
      .entries = depthEntries.data(),
  };
  g_depthLayout = g_device.CreateBindGroupLayout(&depthLayoutDescriptor);
  const wgpu::PipelineLayoutDescriptor depthPipelineLayoutDescriptor{.bindGroupLayoutCount = 1,
                                                                     .bindGroupLayouts = &g_depthLayout};
  g_depthPipelineLayout = g_device.CreatePipelineLayout(&depthPipelineLayoutDescriptor);
  // Depth-only composites must also work when both blur effects are disabled.
  const std::array cloudEntries{
      depthEntries[2],
      wgpu::BindGroupLayoutEntry{
          .binding = 3,
          .visibility = wgpu::ShaderStage::Fragment,
          // Shared by edge, clouds and SSAO (96 bytes); validate the
          // required size against each shader at draw time.
          .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = 0},
      },
  };
  const wgpu::BindGroupLayoutDescriptor cloudLayoutDescriptor{
      .label = "Cloud shadows",
      .entryCount = cloudEntries.size(),
      .entries = cloudEntries.data(),
  };
  g_cloudLayout = g_device.CreateBindGroupLayout(&cloudLayoutDescriptor);
  const wgpu::PipelineLayoutDescriptor cloudPipelineLayoutDescriptor{.bindGroupLayoutCount = 1,
                                                                     .bindGroupLayouts = &g_cloudLayout};
  g_cloudPipelineLayout = g_device.CreatePipelineLayout(&cloudPipelineLayoutDescriptor);
  auto torchEntries = cloudEntries;
  torchEntries[1].buffer.minBindingSize = kTorchUniformSize;
  const wgpu::BindGroupLayoutDescriptor torchLayoutDescriptor{
      .label = "Torch light",
      .entryCount = torchEntries.size(),
      .entries = torchEntries.data(),
  };
  g_torchLayout = g_device.CreateBindGroupLayout(&torchLayoutDescriptor);
  const wgpu::PipelineLayoutDescriptor torchPipelineLayoutDescriptor{.bindGroupLayoutCount = 1,
                                                                     .bindGroupLayouts = &g_torchLayout};
  g_torchPipelineLayout = g_device.CreatePipelineLayout(&torchPipelineLayoutDescriptor);

}

void shutdown() {
  g_farSource = {};
  g_farSourceView = {};
  g_farSourceSize = {};
  g_farSourceFormat = wgpu::TextureFormat::Undefined;
  g_filterPipelines = {};
  g_filterPipelineLayout = {};
  g_filterLayout = {};
  g_filterModule = {};
  g_gradeModule = {};
  g_imageWork = {};
  g_imageWorkView = {};
  g_imageSource = {};
  g_imageSourceView = {};
  g_imageSize = {};
  g_imageFormat = wgpu::TextureFormat::Undefined;
  g_gradePipelines = {};
  g_imageSamples = 0;
  g_resolvedDepth = {};
  g_resolvedDepthView = {};
  g_resolvedDepthSize = {};
  g_resolveDepthLayouts = {};
  g_resolveDepthPipelines = {};
  g_croppedScene = {};
  g_croppedSceneView = {};
  g_croppedSceneSize = {};
  g_croppedSceneFormat = wgpu::TextureFormat::Undefined;
  g_cropComposite = {};
  g_cropCompositeSamples = 0;
  g_bright = g_blurH = g_blurV = g_composite = g_farComposite = {};
  g_cloudPass = {};
  g_cloudSamples = 0;
  g_ssaoPass = {};
  g_ssaoModule = {};
  g_ssaoSamples = 0;
  g_edgePass = {};
  g_edgeSamples = 0;
  g_edgeModule = {};
  g_torchPass = {};
  g_torchSamples = 0;
  g_torchPipelineLayout = {};
  g_torchLayout = {};
  g_torchModule = {};
  g_cloudPipelineLayout = {};
  g_cloudLayout = {};
  g_cloudModule = {};
  g_tex = {};
  g_view = {};
  g_farSamples = 0;
  g_depthPipelineLayout = {};
  g_depthLayout = {};
  g_farModule = {};
  g_size = {};
  g_format = wgpu::TextureFormat::Undefined;
  g_compositeSamples = 0;
  g_sampler = {};
  g_pipelineLayout = {};
  g_layout = {};
  g_module = {};
}

void set_image_filters(bool fxaa, int sharpening) noexcept {
  g_imageFilters.store((std::clamp(sharpening, 0, 2) << 1) | int(fxaa), std::memory_order_relaxed);
}
void set_colour_grade(int preset) noexcept { g_colourGrade.store(std::clamp(preset, 0, 2), std::memory_order_relaxed); }

bool ssao_enabled() noexcept { return g_ssao.load(std::memory_order_relaxed) && webgpu::g_hasCoreFeatures; }
void set_ssao(int level) noexcept { g_ssao.store(std::clamp(level, 0, 2), std::memory_order_relaxed); }

bool bloom_enabled() noexcept { return g_bloom.load(std::memory_order_relaxed); }

void set_bloom(bool enabled) noexcept { g_bloom.store(enabled, std::memory_order_relaxed); }

bool far_blur_enabled() noexcept { return g_farBlur.load(std::memory_order_relaxed) && webgpu::g_hasCoreFeatures; }

void set_far_blur(bool enabled) noexcept { g_farBlur.store(enabled, std::memory_order_relaxed); }

bool cloud_shadows_enabled() noexcept { return g_clouds.load(std::memory_order_relaxed) && webgpu::g_hasCoreFeatures; }

void set_cloud_shadows(bool enabled) noexcept { g_clouds.store(enabled, std::memory_order_relaxed); }

bool torch_light_enabled() noexcept { return g_torch.load(std::memory_order_relaxed) && webgpu::g_hasCoreFeatures; }

void set_torch_light(bool enabled) noexcept { g_torch.store(enabled, std::memory_order_relaxed); }

void set_point_lights(const float* lights, uint32_t count) noexcept {
  std::lock_guard lock{g_cameraMutex};
  g_lightCount = lights != nullptr ? std::min(count, kMaxLights) : 0;
  if (g_lightCount > 0 && lights != nullptr)
    std::memcpy(g_lights.data(), lights, g_lightCount * 8 * sizeof(float));
}

bool edge_light_enabled() noexcept { return g_edge.load(std::memory_order_relaxed) && webgpu::g_hasCoreFeatures; }

void set_edge_light(bool enabled) noexcept { g_edge.store(enabled, std::memory_order_relaxed); }

void set_cloud_weather(float x, float z, float coverage, float sunlight) noexcept {
  std::lock_guard lock{g_cameraMutex};
  g_cloudWeather = std::isfinite(x) && std::isfinite(z) && std::isfinite(coverage) && std::isfinite(sunlight)
      ? std::array<float, 4>{x, z, std::clamp(coverage, 0.f, 1.f), std::clamp(sunlight, 0.f, 1.f)}
      : std::array<float, 4>{};
}

void set_sun_direction(float x, float y, float z) noexcept {
  const float length = std::sqrt(x * x + y * y + z * z);
  std::lock_guard lock{g_cameraMutex};
  if (std::isfinite(length) && length > 0.f) {
    g_camera[17] = x / length;
    g_camera[18] = y / length;
    g_camera[19] = z / length;
  } else {
    g_camera[17] = g_camera[18] = g_camera[19] = 0.f;
  }
}

void set_world_view(const float invView[12], float tanHalfFovy, float aspect, float nearPlane, float seaLevel,
                    float seconds, float farPlane) noexcept {
  std::lock_guard lock{g_cameraMutex};
  g_cameraValid = invView != nullptr && std::isfinite(nearPlane) && nearPlane > 0.f &&
      std::isfinite(farPlane) && farPlane > nearPlane &&
      std::isfinite(tanHalfFovy) && tanHalfFovy > 0.f &&
      std::isfinite(aspect) && aspect > 0.f && std::isfinite(seaLevel) && std::isfinite(seconds);
  if (g_cameraValid) {
    for (unsigned i = 0; i < 12; ++i) g_cameraValid = g_cameraValid && std::isfinite(invView[i]);
  }
  if (!g_cameraValid) return;
  g_depthConversion = depth_conversion(nearPlane,farPlane);
  std::memcpy(g_camera.data(), invView, 12 * sizeof(float));
  g_camera[12] = tanHalfFovy;
  g_camera[13] = aspect;
  g_camera[14] = nearPlane;
  g_camera[15] = seaLevel;
  g_camera[16] = seconds;
}

Camera current_camera() noexcept {
  Camera camera;
  std::lock_guard lock{g_cameraMutex};
  camera.view = g_camera;
  camera.cloudWeather = g_cloudWeather;
  camera.depthConversion = g_depthConversion;
  camera.valid = g_cameraValid;
  camera.lights = g_lights;
  camera.lightCount = g_lightCount;
  return camera;
}

bool any_enabled() noexcept {
  return bloom_enabled() || far_blur_enabled() || cloud_shadows_enabled() ||
         edge_light_enabled() || torch_light_enabled() || ssao_enabled() ||
         (webgpu::g_hasCoreFeatures && (g_colourGrade.load(std::memory_order_relaxed) != 0 ||
          g_imageFilters.load(std::memory_order_relaxed) != 0));
}

State snapshot() noexcept {
  const int filters = webgpu::g_hasCoreFeatures ? g_imageFilters.load(std::memory_order_relaxed) : 0;
  return {.camera = current_camera(),
          .bloom = bloom_enabled(), .farBlur = far_blur_enabled(),
          .clouds = cloud_shadows_enabled(),
          .edge = edge_light_enabled(), .torch = torch_light_enabled(), .ssao = webgpu::g_hasCoreFeatures ? g_ssao.load(std::memory_order_relaxed) : 0,
          .colourGrade = webgpu::g_hasCoreFeatures ? g_colourGrade.load(std::memory_order_relaxed) : 0,
          .fxaa = (filters & 1) != 0, .sharpening = filters >> 1};
}

void encode_effects(const wgpu::CommandEncoder& cmd, const wgpu::Texture& sceneTexture, const wgpu::TextureView& scene, const wgpu::TextureView& depthView,
            const wgpu::Extent3D& size, wgpu::TextureFormat format, const wgpu::TextureView& dst,
            const wgpu::TextureView& resolve, uint32_t msaaSamples, const State& state, bool depthConverted = false) {
  const Camera& camera = state.camera;
  // Overlay passes must inherit the world's cutscene crop, not cover the bars.
  const auto x = std::min(state.worldScissor[0], size.width);
  const auto y = std::min(state.worldScissor[1], size.height);
  const std::array<uint32_t, 4> scissor{x, y,
      std::min(state.worldScissor[2], size.width - x),
      std::min(state.worldScissor[3], size.height - y)};
  if (!scissor[2] || !scissor[3]) return;
  // Resolve only when an enabled effect needs depth; bloom uses color alone.
  const bool needsDepth = state.farBlur || state.edge || state.torch || state.clouds || state.ssao;
  const auto effectDepth = needsDepth && !depthConverted ? resolve_effect_depth(cmd,depthView,size,msaaSamples,camera) : depthView;
  if (state.ssao && camera.valid && effectDepth) {
    const webgpu::gpu_prof::Zone zone{cmd, "Ambient occlusion"};
    ensure_targets(size, format, msaaSamples, state);
    std::array<float, 24> uniform{};
    std::copy(camera.view.begin(), camera.view.end(), uniform.begin());
    // SSAO does not use camera time; reuse that slot for the captured intensity.
    uniform[16] = state.ssao == 2 ? 2.5f : 1.f;
    for (unsigned i = 0; i < 4; ++i) uniform[20 + i] = float(scissor[i]);
    draw_with_uniform(cmd, "Ambient occlusion", g_ssaoPass, g_cloudLayout, uniform.data(), sizeof(uniform),
                      effectDepth, dst, resolve, &scissor);
  }
  if (state.edge && g_module && effectDepth) {
    if (camera.valid) {
      const webgpu::gpu_prof::Zone zone{cmd, "Edge light"};
      ensure_targets(size, format, msaaSamples, state);
      std::array<float,24> uniform{};
      std::copy(camera.view.begin(),camera.view.end(),uniform.begin());
      for(unsigned i=0;i<4;++i) uniform[20+i]=float(scissor[i]);
      draw_with_uniform(cmd, "Edge light", g_edgePass, g_cloudLayout, uniform.data(), sizeof(uniform),
                        effectDepth, dst, resolve, &scissor);
    }
  }
  if (state.torch && g_module && effectDepth) {
    if (camera.valid && camera.lightCount > 0) {
      std::array<float, kTorchUniformSize / sizeof(float)> uniform{};
      std::memcpy(uniform.data(), camera.view.data(), sizeof(camera.view));
      uniform[20] = static_cast<float>(camera.lightCount);
      for (unsigned i=0;i<4;++i) uniform[24+i]=float(scissor[i]);
      std::memcpy(uniform.data() + 28, camera.lights.data(), sizeof(camera.lights));
      const webgpu::gpu_prof::Zone zone{cmd, "Torch light"};
      ensure_targets(size, format, msaaSamples, state);
      draw_with_uniform(cmd, "Torch light", g_torchPass, g_torchLayout, uniform.data(), sizeof(uniform), effectDepth,
                        dst, resolve, &scissor);
    }
  }
  if (state.clouds && g_module && effectDepth) {
    if (camera.valid) {
      const webgpu::gpu_prof::Zone zone{cmd, "Cloud shadows"};
      ensure_targets(size, format, msaaSamples, state);
      std::array<float, 24> uniform{};
      std::copy(camera.view.begin(), camera.view.end(), uniform.begin());
      std::copy(camera.cloudWeather.begin(), camera.cloudWeather.end(), uniform.begin() + 20);
      draw_with_uniform(cmd, "Cloud shadows", g_cloudPass, g_cloudLayout, uniform.data(), sizeof(uniform),
                        effectDepth, dst, resolve, &scissor);
    }
  }
  if (state.farBlur && g_module && effectDepth && size.width >= 4 && size.height >= 4) {
    const webgpu::gpu_prof::Zone zone{cmd, "Far blur"};
    ensure_targets(size, format, msaaSamples, state);
    if (!g_farSource || g_farSourceFormat != format || g_farSourceSize.width != size.width ||
        g_farSourceSize.height != size.height) {
      const wgpu::TextureDescriptor desc{.label = "Depth-aware blur source",
          .usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::TextureBinding,
          .dimension = wgpu::TextureDimension::e2D, .size = size, .format = format};
      g_farSource = g_device.CreateTexture(&desc);
      g_farSourceView = g_farSource.CreateView();
      g_farSourceSize = size;
      g_farSourceFormat = format;
    }
    const wgpu::TexelCopyTextureInfo source{.texture = sceneTexture};
    const wgpu::TexelCopyTextureInfo target{.texture = g_farSource};
    cmd.CopyTextureToTexture(&source, &target, &size);
    draw_depth(cmd, "Far blur composite", g_farComposite, g_farSourceView, effectDepth, dst, resolve,
               true, scissor, crop_uniform(size, scissor));
  }
  if (state.bloom)
    encode_bloom(cmd, scene, size, format, dst, resolve, msaaSamples, scissor);
}

// One reusable scratch image prevents sampling from an active render attachment.
void ensure_image_source(const wgpu::Extent3D& size, wgpu::TextureFormat format, uint32_t samples) {
  if (g_imageFormat != format || g_imageSamples != samples) {
    g_gradePipelines = {};
    g_filterPipelines = {};
    g_imageSamples = samples;
  }
  if (!g_imageSource || g_imageFormat != format ||
      g_imageSize.width != size.width || g_imageSize.height != size.height) {
    const wgpu::TextureDescriptor descriptor{
        .label = "Post-processing source",
        .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::RenderAttachment,
        .dimension = wgpu::TextureDimension::e2D,
        .size = size, .format = format};
    g_imageWork = {};
    g_imageWorkView = {};
    g_imageSource = g_device.CreateTexture(&descriptor);
    g_imageSourceView = g_imageSource.CreateView();
    g_imageSize = size;
    g_imageFormat = format;
  }
}

// Final image filters run after the game's upscale. A single source copy feeds
// alternating scratch attachments; only the last pass writes back to the EFB.
void encode_image_filters(const wgpu::CommandEncoder& cmd, const wgpu::Texture& sceneTexture,
                          const wgpu::Extent3D& size, wgpu::TextureFormat format,
                          const wgpu::TextureView& dst, const wgpu::TextureView& resolve,
                          uint32_t samples, const State& state) {
  if (!state.image() || !g_module || !sceneTexture || !dst || !size.width || !size.height) return;
  const auto x = std::min(state.worldScissor[0], size.width);
  const auto y = std::min(state.worldScissor[1], size.height);
  const std::array<uint32_t, 4> scissor{x, y,
      std::min(state.worldScissor[2], size.width - x),
      std::min(state.worldScissor[3], size.height - y)};
  if (!scissor[2] || !scissor[3]) return;
  ensure_image_source(size, format, samples);
  if ((state.fxaa || state.sharpening) && !g_filterModule) {
    wgpu::ShaderSourceWGSL code{};
    code.code = shaders::kImageFilterShader;
    const wgpu::ShaderModuleDescriptor descriptor{.nextInChain = &code, .label = "Image filters"};
    g_filterModule = g_device.CreateShaderModule(&descriptor);
    const std::array entries{
        wgpu::BindGroupLayoutEntry{.binding = 0, .visibility = wgpu::ShaderStage::Fragment,
            .sampler = {.type = wgpu::SamplerBindingType::Filtering}},
        wgpu::BindGroupLayoutEntry{.binding = 1, .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D}},
        wgpu::BindGroupLayoutEntry{.binding = 4, .visibility = wgpu::ShaderStage::Fragment,
            .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = 32}}};
    const wgpu::BindGroupLayoutDescriptor bindDesc{.entryCount = entries.size(), .entries = entries.data()};
    g_filterLayout = g_device.CreateBindGroupLayout(&bindDesc);
    const wgpu::PipelineLayoutDescriptor layoutDesc{.bindGroupLayoutCount = 1, .bindGroupLayouts = &g_filterLayout};
    g_filterPipelineLayout = g_device.CreatePipelineLayout(&layoutDesc);
  }
  if (state.colourGrade && !g_gradeModule) {
    wgpu::ShaderSourceWGSL code{};
    code.code = shaders::kGradeShader;
    const wgpu::ShaderModuleDescriptor descriptor{.nextInChain = &code, .label = "Colour grading"};
    g_gradeModule = g_device.CreateShaderModule(&descriptor);
  }
  const auto uniform = image_uniform({float(x)/size.width,float(y)/size.height,
      float(scissor[2])/size.width,float(scissor[3])/size.height,
      state.sharpening == 2 ? 1.2f : .6f, state.sharpening == 2 ? 2.f : 1.f,
      state.sharpening == 2 ? .05f : .025f, 0.f});
  const int count = int(state.fxaa) + int(state.sharpening != 0) + int(state.colourGrade != 0);
  if (count > 1 && !g_imageWork) {
    const wgpu::TextureDescriptor descriptor{.label = "Post-processing intermediate",
        .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment,
        .dimension = wgpu::TextureDimension::e2D, .size = size, .format = format};
    g_imageWork = g_device.CreateTexture(&descriptor);
    g_imageWorkView = g_imageWork.CreateView();
  }
  const wgpu::TexelCopyTextureInfo source{.texture = sceneTexture};
  const wgpu::TexelCopyTextureInfo target{.texture = g_imageSource};
  cmd.CopyTextureToTexture(&source, &target, &size);
  auto input = g_imageSourceView;
  auto output = g_imageWorkView;
  int remaining = count;
  for (unsigned stage = 0; stage < 3; ++stage) {
    if (stage == 0 ? !state.fxaa : stage == 1 ? !state.sharpening : !state.colourGrade) continue;
    const bool last = --remaining == 0;
    const uint32_t passSamples = last ? samples : 1;
    const char* label = stage == 0 ? "FXAA" : stage == 1 ? "Sharpening" : "Colour grading";
    const webgpu::gpu_prof::Zone zone{cmd, label};
    auto& pipeline = stage < 2 ? g_filterPipelines[stage][passSamples > 1 ? 1 : 0]
                               : g_gradePipelines[state.colourGrade - 1];
    if (!pipeline) {
      const char* entry = stage == 0 ? "fs_fxaa" : stage == 1 ? "fs_sharpen"
          : state.colourGrade == 1 ? "fs_grade_vivid" : "fs_grade_warm";
      pipeline = make_pipeline(label,entry,format,passSamples,kReplace,
          stage < 2 ? g_filterModule : g_gradeModule,
          stage < 2 ? g_filterPipelineLayout : g_pipelineLayout);
    }
    draw(cmd,label,pipeline,input,last ? dst : output,last,last ? resolve : wgpu::TextureView{},
         nullptr,&scissor,uniform);
    if (!last) std::swap(input,output);
  }
}

// Native dynamic resolution keeps world color/depth in the top-left EFB
// rectangle until the game stretches it. Work on matching cropped textures,
// then write only that rectangle back, preserving the ordinary upscale/HUD path.
void encode(const wgpu::CommandEncoder& cmd, const wgpu::Texture& sceneTexture,
            const wgpu::TextureView& scene, const wgpu::TextureView& depthView,
            const wgpu::Extent3D& size, wgpu::TextureFormat format, const wgpu::TextureView& dst,
            const wgpu::TextureView& resolve, uint32_t msaaSamples, const State& state) {
  if (!g_module || !scene || !dst || size.width == 0 || size.height == 0) return;
  const auto region = active_region(size.width,size.height,state.contentScale);
  if (!region.cropped) {
    encode_effects(cmd,sceneTexture,scene,depthView,size,format,dst,resolve,msaaSamples,state);

    return;
  }
  const wgpu::Extent3D active{
      .width = region.width,
      .height = region.height,
      .depthOrArrayLayers = 1};
  if (!g_croppedScene || g_croppedSceneSize.width != active.width ||
      g_croppedSceneSize.height != active.height || g_croppedSceneFormat != format) {
    const wgpu::TextureDescriptor descriptor{
        .label = "HD active scene region",
        .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::RenderAttachment,
        .dimension = wgpu::TextureDimension::e2D,
        .size = active,
        .format = format};
    g_croppedScene = g_device.CreateTexture(&descriptor);
    g_croppedSceneView = g_croppedScene.CreateView();
    g_croppedSceneSize = active;
    g_croppedSceneFormat = format;
    g_cropComposite = {};
  }
  const wgpu::TexelCopyTextureInfo source{.texture = sceneTexture};
  const wgpu::TexelCopyTextureInfo target{.texture = g_croppedScene};
  cmd.CopyTextureToTexture(&source,&target,&active);
  const bool needsDepth = state.farBlur || state.edge || state.torch || state.clouds || state.ssao;
  const auto depth = needsDepth ? resolve_effect_depth(cmd,depthView,active,msaaSamples,state.camera) : wgpu::TextureView{};
  encode_effects(cmd,g_croppedScene,g_croppedSceneView,depth,active,format,g_croppedSceneView,nullptr,1,state,true);

  if (!g_cropComposite || g_cropCompositeSamples != msaaSamples) {
    g_cropComposite = make_pipeline("HD active scene composite","fs_copy",format,msaaSamples,kReplace);
    g_cropCompositeSamples = msaaSamples;
  }
  draw(cmd,"HD active scene composite",g_cropComposite,g_croppedSceneView,dst,true,resolve,&active);
}

void encode_bloom(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& scene, const wgpu::Extent3D& size,
                  wgpu::TextureFormat format, const wgpu::TextureView& dst, const wgpu::TextureView& resolve,
                  uint32_t msaaSamples, const std::array<uint32_t, 4>& scissor) {
  if (!g_module || !scene || !dst || size.width < 4 || size.height < 4) {
    return;
  }
  const webgpu::gpu_prof::Zone zone{cmd, "Bloom"};
  State state{};
  state.bloom = true;
  ensure_targets(size, format, msaaSamples, state);
  const auto bounds = crop_uniform(size, scissor);
  draw(cmd, "Bloom bright pass", g_bright, scene, g_view[0], false, nullptr, nullptr, nullptr, bounds);
  for (int i = 0; i < 2; ++i) {
    draw(cmd, "Bloom blur (horizontal)", g_blurH, g_view[0], g_view[1], false, nullptr, nullptr, nullptr, bounds);
    draw(cmd, "Bloom blur (vertical)", g_blurV, g_view[1], g_view[0], false, nullptr, nullptr, nullptr, bounds);
  }
  draw(cmd, "Bloom composite", g_composite, g_view[0], dst, true, resolve, nullptr, &scissor, bounds);
}
} // namespace aurora::gfx::post_fx

void aurora_set_image_filters(bool fxaa, int sharpening) { aurora::gfx::post_fx::set_image_filters(fxaa, sharpening); }
void aurora_set_colour_grade(int preset) { aurora::gfx::post_fx::set_colour_grade(preset); }
void aurora_set_ssao(int level) { aurora::gfx::post_fx::set_ssao(level); }
void aurora_set_bloom(bool enabled) { aurora::gfx::post_fx::set_bloom(enabled); }

void aurora_set_far_blur(bool enabled) { aurora::gfx::post_fx::set_far_blur(enabled); }

void aurora_set_cloud_weather(float x, float z, float coverage, float sunlight) {
  aurora::gfx::post_fx::set_cloud_weather(x, z, coverage, sunlight);
}
void aurora_set_cloud_shadows(bool enabled) { aurora::gfx::post_fx::set_cloud_shadows(enabled); }

void aurora_set_edge_light(bool enabled) { aurora::gfx::post_fx::set_edge_light(enabled); }

void aurora_set_torch_light(bool enabled) { aurora::gfx::post_fx::set_torch_light(enabled); }

void aurora_set_world_view(const float inv_view[12], float tan_half_fovy, float aspect,
                           float near_plane, float sea_level, float seconds, float farPlane) {
  aurora::gfx::post_fx::set_world_view(inv_view, tan_half_fovy, aspect, near_plane, sea_level, seconds, farPlane);
}

void aurora_set_sun_direction(float x, float y, float z) { aurora::gfx::post_fx::set_sun_direction(x, y, z); }

void aurora_set_point_lights(const float* lights, uint32_t count) {
  aurora::gfx::post_fx::set_point_lights(lights, count);
}
