#pragma once
// WGSL adapted from BlueWake; kept separate from device/resource lifetime code.
namespace aurora::gfx::post_fx::shaders {

inline constexpr const char* kResolveDepthShader = R"(
@group(0) @binding(1) var<uniform> conversion: vec4f;
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let positions = array<vec2f,3>(vec2f(-1,1),vec2f(-1,-3),vec2f(3,1));
    return vec4f(positions[i],0,1);
}
@fragment fn fs_main(@builtin(position) position: vec4f) -> @builtin(frag_depth) f32 {
    let pixel = vec2i(position.xy);
    let native = load_depth(pixel);
    // Preserve the clear/background sentinel. Convert finite-far depth to the
    // infinite-far form consumed by all effect shaders (near / distance).
    return select(0.0, native * conversion.x + conversion.y, native > 0.0);
}
)";

inline constexpr const char* kShader = R"""(
override threshold: f32 = 0.72;
override strength: f32 = 0.35;

@group(0) @binding(0) var samp: sampler;
@group(0) @binding(1) var src: texture_2d<f32>;

struct ImageBounds {
    clip: vec4<f32>,
    unused: vec4<f32>,
};
@group(0) @binding(4) var<uniform> bounds: ImageBounds;
fn sample_colour(uv: vec2<f32>) -> vec4<f32> {
    let halfTexel = 0.5 / vec2<f32>(textureDimensions(src));
    let lo = bounds.clip.xy + halfTexel;
    let hi = max(lo, bounds.clip.xy + bounds.clip.zw - halfTexel);
    return textureSampleLevel(src, samp, clamp(uv, lo, hi), 0.0);
}
struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

var<private> pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(vec2(-1.0, 1.0), vec2(-1.0, -3.0), vec2(3.0, 1.0));
var<private> uvs: array<vec2<f32>, 3> = array<vec2<f32>, 3>(vec2(0.0, 0.0), vec2(0.0, 2.0), vec2(2.0, 0.0));
var<private> weights: array<f32, 5> = array<f32, 5>(0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4<f32>(pos[i], 0.0, 1.0);
    out.uv = uvs[i];
    return out;
}

fn texel() -> vec2<f32> {
    return 1.0 / vec2<f32>(textureDimensions(src));
}

// A quarter of the size: four bilinear taps cover a 4x4 block of the scene.
// Only what is brighter than the threshold passes, with a soft knee.
@fragment
fn fs_bright(in: VertexOutput) -> @location(0) vec4<f32> {
    let t = texel();
    let c = 0.25 * (sample_colour( in.uv + t * vec2(-1.0, -1.0)).rgb +
                    sample_colour( in.uv + t * vec2(1.0, -1.0)).rgb +
                    sample_colour( in.uv + t * vec2(-1.0, 1.0)).rgb +
                    sample_colour( in.uv + t * vec2(1.0, 1.0)).rgb);
    let luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return vec4(c * smoothstep(threshold, 1.0, luma), 1.0);
}

fn blur(uv: vec2<f32>, dir: vec2<f32>) -> vec4<f32> {
    let step = texel() * dir * 1.5;
    var c = sample_colour( uv).rgb * weights[0];
    for (var i = 1; i < 5; i++) {
        let o = step * f32(i);
        c += (sample_colour( uv + o).rgb + sample_colour( uv - o).rgb) * weights[i];
    }
    return vec4(c, 1.0);
}

@fragment
fn fs_blur_h(in: VertexOutput) -> @location(0) vec4<f32> {
    return blur(in.uv, vec2(1.0, 0.0));
}

@fragment
fn fs_blur_v(in: VertexOutput) -> @location(0) vec4<f32> {
    return blur(in.uv, vec2(0.0, 1.0));
}

// Copy an already processed active region without another blur or alpha change.
@fragment
fn fs_copy(in: VertexOutput) -> @location(0) vec4<f32> {
    return sample_colour(in.uv);
}

// Half an 8-bit step of noise (interleaved gradient noise): a smooth shade
// multiplied or added into the 8-bit scene otherwise shows as bands.
fn dither(pos: vec2<f32>) -> f32 {
    return (fract(52.9829189 * fract(dot(pos, vec2(0.06711056, 0.00583715)))) - 0.5) * (1.5 / 255.0);
}

// Added onto the scene; the EFB's alpha is kept.
@fragment
fn fs_composite(in: VertexOutput) -> @location(0) vec4<f32> {
    let glow = sample_colour( in.uv).rgb * strength;
    return vec4(max(glow + select(0.0, dither(in.pos.xy), max(glow.r, max(glow.g, glow.b)) > 0.002), vec3(0.0)), 0.0);
}
)""";

// Compiled only when a spatial filter is enabled.
// Spatial filters operate on the world at its render resolution, before HUD.
inline constexpr const char* kImageFilterShader = R"""(
@group(0) @binding(0) var samp: sampler;
@group(0) @binding(1) var src: texture_2d<f32>;
struct FilterParams {
    clip: vec4<f32>,
    strength: vec4<f32>,
};
@group(0) @binding(4) var<uniform> params: FilterParams;
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
    let positions = array<vec2<f32>,3>(vec2(-1.0,1.0),vec2(-1.0,-3.0),vec2(3.0,1.0));
    return vec4(positions[i],0.0,1.0);
}
fn sample_world(p: vec2<f32>) -> vec4<f32> {
    // Clamp taps to the camera crop so letterbox bars cannot bleed into edges.
    let dims = vec2<f32>(textureDimensions(src));
    let low = params.clip.xy * dims + vec2(0.5);
    let high = max(low, (params.clip.xy + params.clip.zw) * dims - vec2(0.5));
    return textureSampleLevel(src, samp, clamp(p, low, high) / dims, 0.0);
}
fn luma(c: vec3<f32>) -> f32 { return dot(c, vec3(0.299,0.587,0.114)); }
@fragment fn fs_fxaa(@builtin(position) pixel: vec4<f32>) -> @location(0) vec4<f32> {
    let p = pixel.xy;
    let center = sample_world(p);
    let nw = luma(sample_world(p + vec2(-1.0,-1.0)).rgb);
    let ne = luma(sample_world(p + vec2(1.0,-1.0)).rgb);
    let sw = luma(sample_world(p + vec2(-1.0,1.0)).rgb);
    let se = luma(sample_world(p + vec2(1.0,1.0)).rgb);
    let mid = luma(center.rgb);
    let low = min(mid, min(min(nw,ne),min(sw,se)));
    let high = max(mid, max(max(nw,ne),max(sw,se)));
    if (high - low < max(0.0312, high * 0.125)) { return center; }
    let direction = vec2(-((nw+ne)-(sw+se)), (nw+sw)-(ne+se));
    let reduce = max((nw+ne+sw+se) * (0.25 * 0.125), 0.0078125);
    let step = clamp(direction / (min(abs(direction.x),abs(direction.y)) + reduce), vec2(-8.0), vec2(8.0));
    let a = 0.5 * (sample_world(p - step/6.0).rgb + sample_world(p + step/6.0).rgb);
    let b = 0.5 * a + 0.25 * (sample_world(p - step*0.5).rgb + sample_world(p + step*0.5).rgb);
    let result = select(b, a, luma(b) < low || luma(b) > high);
    return vec4(result, center.a);
}
@fragment fn fs_sharpen(@builtin(position) pixel: vec4<f32>) -> @location(0) vec4<f32> {
    let p = pixel.xy;
    let center = sample_world(p);
    let radius = params.strength.y;
    let n = sample_world(p + vec2(0.0,-radius)).rgb;
    let s = sample_world(p + vec2(0.0,radius)).rgb;
    let e = sample_world(p + vec2(radius,0.0)).rgb;
    let w = sample_world(p + vec2(-radius,0.0)).rgb;
    let low = min(center.rgb,min(min(n,s),min(e,w)));
    let high = max(center.rgb,max(max(n,s),max(e,w)));
    let detail = center.rgb - (n+s+e+w)*0.25;
    // Exact local min/max clamping erases sharpening at texture extrema.
    // Permit a small, contrast-dependent excursion, bounded in absolute colour.
    let allowance = min(vec3(params.strength.z), (high-low)*0.25);
    let boost = clamp(detail*params.strength.x, -allowance, allowance);
    return vec4(clamp(center.rgb + boost, vec3(0.0), vec3(1.0)),center.a);
}
)""";

// Compiled only when a colour preset is selected, independently of bloom/blur.
inline constexpr const char* kGradeShader = R"""(
@group(0) @binding(0) var samp: sampler;
@group(0) @binding(1) var src: texture_2d<f32>;
struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> VertexOutput {
    let positions = array<vec2<f32>,3>(vec2(-1.0,1.0),vec2(-1.0,-3.0),vec2(3.0,1.0));
    let coords = array<vec2<f32>,3>(vec2(0.0,0.0),vec2(0.0,2.0),vec2(2.0,0.0));
    var out: VertexOutput;
    out.pos = vec4(positions[i],0.0,1.0);
    out.uv = coords[i];
    return out;
}
// SDR grading with distinct presets. The contrast curve preserves black and white; the
// saturation change is limited to the available gamut instead of clipping it.
fn grade(c: vec3<f32>, contrastAmount: f32, saturation: f32, warmth: f32) -> vec3<f32> {
    let contrast = c + contrastAmount * c * (vec3(1.0) - c) * (2.0 * c - vec3(1.0));
    let luma = dot(contrast, vec3(0.2126, 0.7152, 0.0722));
    let delta = contrast - vec3(luma);
    var scale = saturation;
    for (var i = 0; i < 3; i++) {
        if (delta[i] > 0.00001) { scale = min(scale, (1.0 - luma) / delta[i]); }
        if (delta[i] < -0.00001) { scale = min(scale, -luma / delta[i]); }
    }
    let colour = vec3(luma) + delta * scale;
    // Tint only midtones: black, white and scene alpha are preserved.
    return clamp(colour + warmth * colour * (vec3(1.0) - colour) * vec3(1.0, 0.15, -1.0),
                 vec3(0.0), vec3(1.0));
}
@fragment fn fs_grade_vivid(in: VertexOutput) -> @location(0) vec4<f32> {
    let c = textureSample(src, samp, in.uv);
    return vec4(grade(c.rgb, 0.45, 1.22, 0.0), c.a);
}
@fragment fn fs_grade_warm(in: VertexOutput) -> @location(0) vec4<f32> {
    let c = textureSample(src, samp, in.uv);
    return vec4(grade(c.rgb, 0.25, 1.06, 0.24), c.a);
}

)""";

inline constexpr const char* kFarShader = R"""(
override far_start: f32 = 8000.0;
override far_end: f32 = 30000.0;
override amount: f32 = 0.8;
override background: f32 = 0.35;
override debug_view: bool = false;

@group(0) @binding(0) var samp: sampler;
@group(0) @binding(1) var blurred: texture_2d<f32>;
@group(0) @binding(2) var depth: texture_depth_2d;

struct ImageBounds {
    clip: vec4<f32>,
    unused: vec4<f32>,
};
@group(0) @binding(4) var<uniform> bounds: ImageBounds;

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

var<private> pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(vec2(-1.0, 1.0), vec2(-1.0, -3.0), vec2(3.0, 1.0));
var<private> uvs: array<vec2<f32>, 3> = array<vec2<f32>, 3>(vec2(0.0, 0.0), vec2(0.0, 2.0), vec2(2.0, 0.0));

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4<f32>(pos[i], 0.0, 1.0);
    out.uv = uvs[i];
    return out;
}

@fragment
fn fs_far(in: VertexOutput) -> @location(0) vec4<f32> {
    let d = textureLoad(depth, vec2<i32>(in.pos.xy), 0);
    // No depth: what the game draws behind the scene without it (the sky, the
    // sea toward the horizon, the islands' far models).
    let f = select(smoothstep(far_start, far_end, 1.0 / max(d, 1e-9)) * amount, background, d <= 0.0);
    if (debug_view) {
        // log2 of the distance as brightness (0 at 1, 1 at 2^24), the sky blue.
        if (d <= 0.0) {
            return vec4(0.0, 0.0, 1.0, 1.0);
        }
        return vec4(vec3(log2(1.0 / d) / 24.0), 1.0);
    }
    if (f <= 0.001) { return vec4(0.0); }
    let dims = vec2<i32>(textureDimensions(depth));
    let lo = vec2<i32>(ceil(bounds.clip.xy * vec2<f32>(dims)));
    let hi = max(lo, vec2<i32>(floor((bounds.clip.xy+bounds.clip.zw)*vec2<f32>(dims))) - vec2(1));
    let here = vec2<i32>(in.pos.xy);
    let spacing = max(1.0, f32(dims.y) / 1080.0 * 2.0);
    var colour = vec3(0.0);
    var total = 0.0;
    for (var y = -2; y <= 2; y++) {
        for (var x = -2; x <= 2; x++) {
            let r2 = x*x+y*y;
            if (r2 > 4) { continue; }
            let q = clamp(here + vec2<i32>(round(vec2<f32>(f32(x),f32(y))*spacing)),lo,hi);
            let sd = textureLoad(depth,q,0);
            // Do not mix sky and geometry, or distinct foreground/background layers.
            if ((d <= 0.0) != (sd <= 0.0)) { continue; }
            let delta = abs(sd-d) / max(max(sd,d),1e-9);
            let weight = (1.0-smoothstep(0.02,0.08,delta)) / (1.0+f32(r2));
            colour += textureLoad(blurred,q,0).rgb * weight;
            total += weight;
        }
    }
    return vec4(colour / max(total,1e-6), f);
}
)""";

// Local contact AO. Fixed sampling avoids temporal noise; no screen-space blur
// spreads darkness across silhouettes. All distances are in game world units.
inline constexpr const char* kSsaoShader = R"""(
struct View {
    rows: array<vec4<f32>, 3>,
    lens: vec4<f32>,
    time: vec4<f32>,
    clip: vec4<f32>,
};
@group(0) @binding(2) var depth: texture_depth_2d;
@group(0) @binding(3) var<uniform> view: View;
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
    let positions = array<vec2<f32>,3>(vec2(-1.0,1.0),vec2(-1.0,-3.0),vec2(3.0,1.0));
    return vec4(positions[i],0.0,1.0);
}
fn inside(p: vec2<i32>) -> bool {
    let low = vec2<i32>(view.clip.xy);
    let high = low + vec2<i32>(view.clip.zw);
    return all(p >= low) && all(p < high);
}
fn position(p: vec2<i32>, d: f32) -> vec3<f32> {
    let uv = (vec2<f32>(p) + vec2(0.5)) / vec2<f32>(textureDimensions(depth));
    let z = view.lens.z / d;
    return vec3((uv.x * 2.0 - 1.0) * view.lens.x * view.lens.y * z,
                (1.0 - uv.y * 2.0) * view.lens.x * z, -z);
}
@fragment fn fs_ssao(@builtin(position) pixel: vec4<f32>) -> @location(0) vec4<f32> {
    let p = vec2<i32>(pixel.xy);
    let d = textureLoad(depth, p, 0);
    if (d <= 0.0 || !inside(p - vec2(1)) || !inside(p + vec2(1))) { return vec4(1.0); }
    let center = position(p, d);
    if (-center.z >= 5000.0) { return vec4(1.0); }
    let dl = textureLoad(depth, p + vec2(-1,0), 0);
    let dr = textureLoad(depth, p + vec2(1,0), 0);
    let du = textureLoad(depth, p + vec2(0,-1), 0);
    let db = textureLoad(depth, p + vec2(0,1), 0);
    if (min(min(dl,dr), min(du,db)) <= 0.0) { return vec4(1.0); }
    // Use the closer neighbour on each axis to avoid normals across depth edges.
    let left = center - position(p + vec2(-1,0), dl);
    let right = position(p + vec2(1,0), dr) - center;
    let up = position(p + vec2(0,-1), du) - center;
    let down = center - position(p + vec2(0,1), db);
    let dx = select(right, left, abs(left.z) < abs(right.z));
    let dy = select(down, up, abs(up.z) < abs(down.z));
    let crossNormal = cross(dx, dy);
    let normalLength = length(crossNormal);
    if (normalLength < 0.00001) { return vec4(1.0); }
    let normal = crossNormal / normalLength;
    let radius = 35.0;
    let dimensions = vec2<f32>(textureDimensions(depth));
    let projectedRadius = radius * dimensions.y / (2.0 * view.lens.x * -center.z);
    if (projectedRadius < 1.0) { return vec4(1.0); }
    let sampleRadius = min(projectedRadius, 48.0);
    var occlusion = 0.0;
    // Two staggered rings: stable, spatially distributed sampling, no frame jitter.
    for (var i = 0; i < 16; i = i + 1) {
        let outer = i >= 8;
        let angle = f32(i % 8) * 0.785398163 + select(0.0, 0.392699082, outer);
        let offset = vec2(cos(angle), sin(angle)) * sampleRadius * select(0.45, 1.0, outer);
        let q = p + vec2<i32>(round(offset));
        if (!inside(q) || all(q == p)) { continue; }
        let sd = textureLoad(depth, q, 0);
        if (sd <= 0.0) { continue; }
        let delta = position(q, sd) - center;
        let distance = length(delta);
        if (distance <= 0.001 || distance >= radius) { continue; }
        // A small world-space bias rejects self-shadowing on smooth surfaces.
        let facing = max(0.0, (dot(normal, delta) - 1.0) / distance - 0.08);
        let falloff = 1.0 - distance / radius;
        occlusion += facing * falloff * falloff;
    }
    let distanceFade = 1.0 - smoothstep(2500.0, 5000.0, -center.z);
    // time.x carries SSAO intensity, not animation time.
    let amount = min(min(0.35, 0.18 * view.time.x), occlusion * (1.5 / 16.0) * view.time.x) * distanceFade;
    return vec4(vec3(1.0 - amount), 1.0);
}
)""";

inline constexpr const char* kCloudShader = R"""(
override strength: f32 = 0.28;
override scale: f32 = 2200.0;
override debug_view: bool = false;

struct View {
    rows: array<vec4<f32>, 3>,  // the inverse view matrix (view to world), by rows
    lens: vec4<f32>,            // tan(fovy / 2), aspect, near, the sea's height
    time: vec4<f32>,            // seconds, direction to sun XYZ
    weather: vec4<f32>,         // wind offset X/Z, cloud coverage, sunlight
};

@group(0) @binding(2) var depth: texture_depth_2d;
@group(0) @binding(3) var<uniform> view: View;

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

var<private> pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(vec2(-1.0, 1.0), vec2(-1.0, -3.0), vec2(3.0, 1.0));
var<private> uvs: array<vec2<f32>, 3> = array<vec2<f32>, 3>(vec2(0.0, 0.0), vec2(0.0, 2.0), vec2(2.0, 0.0));

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4<f32>(pos[i], 0.0, 1.0);
    out.uv = uvs[i];
    return out;
}

// A gradient for each lattice point, from its integer coordinates: no sin(), so
// every GPU gets the same values whatever its precision.
fn gradient(p: vec2<f32>) -> vec2<f32> {
    var q = bitcast<vec2<u32>>(vec2<i32>(p));
    q = q * 1664525u + 1013904223u;
    q.x += q.y * 1664525u;
    q.y += q.x * 1664525u;
    q = q ^ (q >> vec2(16u));
    q.x += q.y * 1664525u;
    q.y += q.x * 1664525u;
    q = q ^ (q >> vec2(16u));
    let a = f32(q.x) * (6.2831853 / 4294967296.0);
    return vec2(cos(a), sin(a));
}

// Gradient noise, about 0 to 1. Value noise put the clouds' edges along the
// world's X and Z (square-looking shadows); gradient noise has no such rows.
fn gradient_noise(p: vec2<f32>) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    let a = dot(gradient(i), f);
    let b = dot(gradient(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));
    let c = dot(gradient(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));
    let e = dot(gradient(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));
    return 0.5 + 0.9 * mix(mix(a, b, u.x), mix(c, e, u.x), u.y);
}

fn fbm(p0: vec2<f32>) -> f32 {
    // Each octave turned by about 37 degrees, so no two line up.
    let turn = mat2x2<f32>(0.8, 0.6, -0.6, 0.8);
    var p = p0;
    var v = 0.0;
    var a = 0.5;
    for (var i = 0; i < 4; i++) {
        v += a * gradient_noise(p);
        p = turn * p * 2.03 + vec2(17.0, 9.0);
        a *= 0.5;
    }
    return v;
}

// Half an 8-bit step of noise (interleaved gradient noise), so a smooth shade
// multiplied into the 8-bit scene doesn't come out as bands on the sea.
fn dither(pos: vec2<f32>) -> f32 {
    return (fract(52.9829189 * fract(dot(pos, vec2(0.06711056, 0.00583715)))) - 0.5) * (1.5 / 255.0);
}

@fragment
fn fs_clouds(in: VertexOutput) -> @location(0) vec4<f32> {
    let d = textureLoad(depth, vec2<i32>(in.pos.xy), 0);
    let ndc = vec2(in.uv.x * 2.0 - 1.0, 1.0 - in.uv.y * 2.0);
    let ray = vec3(ndc.x * view.lens.x * view.lens.y, ndc.y * view.lens.x, -1.0);
    let dir = vec3(dot(view.rows[0].xyz, ray), dot(view.rows[1].xyz, ray), dot(view.rows[2].xyz, ray));
    let eye = vec3(view.rows[0].w, view.rows[1].w, view.rows[2].w);
    // Where the view ray meets the sea, if it does.
    var sea = -1.0;
    if (dir.y < -1e-4) {
        sea = (view.lens.w - eye.y) / dir.y;
    }
    var world = eye;
    if (d > 0.0 && (sea <= 0.0 || view.lens.z / d < 0.9 * sea)) {
        // Something in front of the sea (an island, a boat): its place from depth.
        world = eye + dir * (view.lens.z / d);
    } else {
        // The sea, also where it has no depth: its place from its plane. Taking
        // the sea's distance from depth too gave straight-edged steps far out
        // wherever depth is less precise (seen on Vulkan).
        if (sea <= 0.0) {
            return vec4(1.0);
        }
        world = eye + dir * sea;
    }
    let distance = length(world - eye);
    if (debug_view) {
        // A 1,000-unit world grid in red and green, height in blue (every 500 units).
        let g = fract(world.xz / 1000.0);
        return vec4(g.x, g.y, fract(world.y / 500.0), 1.0);
    }
    // Moving the sample origin with the wind makes the shadows travel with it.
    let n = fbm((world.xz - view.weather.xy) / scale);
    // Leave clear gaps between patches. The old broad, low threshold shaded
    // most of a close view at once, reading as a global brightness change.
    let threshold = mix(0.65, 0.48, view.weather.z);
    let coverage = smoothstep(threshold, threshold + 0.12, n) * view.weather.z;
    let sunlight = smoothstep(0.02, 0.30, view.time.z) * view.weather.w;
    let shade = coverage * strength * sunlight * (1.0 - smoothstep(60000.0, 160000.0, distance));
    return vec4(vec3(1.0 - shade + select(0.0, dither(in.pos.xy), shade > 0.0)), 1.0);
}
)""";

inline constexpr const char* kEdgeShader = R"""(
override strength: f32 = 0.4;

struct View {
    rows: array<vec4<f32>, 3>,
    lens: vec4<f32>,
    time: vec4<f32>,
    clip: vec4<f32>,
};

@group(0) @binding(2) var depth: texture_depth_2d;
@group(0) @binding(3) var<uniform> view: View;

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

var<private> pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(vec2(-1.0, 1.0), vec2(-1.0, -3.0), vec2(3.0, 1.0));
var<private> uvs: array<vec2<f32>, 3> = array<vec2<f32>, 3>(vec2(0.0, 0.0), vec2(0.0, 2.0), vec2(2.0, 0.0));

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4<f32>(pos[i], 0.0, 1.0);
    out.uv = uvs[i];
    return out;
}

fn inside(p: vec2<i32>) -> bool {
    return all(p >= vec2<i32>(view.clip.xy)) && all(p < vec2<i32>(view.clip.xy+view.clip.zw));
}
fn view_depth(p: vec2<i32>) -> f32 {
    let size = vec2<i32>(textureDimensions(depth));
    let d = textureLoad(depth, clamp(p, vec2<i32>(0), size - vec2<i32>(1)), 0);
    return select(view.lens.z / max(d, 1e-9), 0.0, d <= 0.0);
}

@fragment
fn fs_edge(in: VertexOutput) -> @location(0) vec4<f32> {
    let here = vec2<i32>(in.pos.xy);
    let z = view_depth(here);
    let sun = view.time.yzw;
    if (z <= 0.0 || z > 1500.0 || sun.y < 0.05) {
        return vec4(0.0);
    }
    // The sun's direction in view space, then on screen (y down).
    let l = vec3(dot(vec3(view.rows[0].x, view.rows[1].x, view.rows[2].x), sun),
                 dot(vec3(view.rows[0].y, view.rows[1].y, view.rows[2].y), sun),
                 dot(vec3(view.rows[0].z, view.rows[1].z, view.rows[2].z), sun));
    var dir = vec2(l.x, -l.y);
    if (length(dir) < 0.05) {
        dir = vec2(0.0, -1.0);
    }
    dir = normalize(dir);
    let scale = f32(textureDimensions(depth).y) / 1080.0;
    var rim = 0.0;
    for (var i = 1; i <= 3; i++) {
        let q = here + vec2<i32>(round(dir * f32(i) * 1.5 * scale));
        if (!inside(q)) { continue; }
        let n = view_depth(q);
        if (n <= 0.0 || n > z * 1.5 + 100.0) {
            rim = max(rim, 1.0 - f32(i - 1) / 3.0);
        }
    }
    let fade = 1.0 - smoothstep(800.0, 1500.0, z);
    return vec4(vec3(1.0, 0.95, 0.82) * rim * strength * fade, 0.0);
}
)""";

inline constexpr const char* kTorchShader = R"""(
override strength: f32 = 0.35;
override radius_scale: f32 = 3.0;

struct Light {
    place: vec4<f32>,  // x, y, z, radius
    color: vec4<f32>,  // r, g, b (0 to 1), flicker
};

struct View {
    rows: array<vec4<f32>, 3>,
    lens: vec4<f32>,
    time: vec4<f32>,
    count: vec4<f32>,
    clip: vec4<f32>,
    lights: array<Light, 16>,
};

@group(0) @binding(2) var depth: texture_depth_2d;
@group(0) @binding(3) var<uniform> view: View;

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

var<private> pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(vec2(-1.0, 1.0), vec2(-1.0, -3.0), vec2(3.0, 1.0));
var<private> uvs: array<vec2<f32>, 3> = array<vec2<f32>, 3>(vec2(0.0, 0.0), vec2(0.0, 2.0), vec2(2.0, 0.0));

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4<f32>(pos[i], 0.0, 1.0);
    out.uv = uvs[i];
    return out;
}

fn inside(p: vec2<i32>) -> bool {
    return all(p >= vec2<i32>(view.clip.xy)) && all(p < vec2<i32>(view.clip.xy+view.clip.zw));
}
fn position(p: vec2<i32>, d: f32) -> vec3<f32> {
    let uv = (vec2<f32>(p)+vec2(0.5)) / vec2<f32>(textureDimensions(depth));
    let z = view.lens.z / d;
    return vec3((uv.x*2.0-1.0)*view.lens.x*view.lens.y*z,(1.0-uv.y*2.0)*view.lens.x*z,-z);
}
fn to_view(p: vec3<f32>) -> vec3<f32> {
    let v = p - vec3(view.rows[0].w,view.rows[1].w,view.rows[2].w);
    return vec3(dot(v,vec3(view.rows[0].x,view.rows[1].x,view.rows[2].x)),
                dot(v,vec3(view.rows[0].y,view.rows[1].y,view.rows[2].y)),
                dot(v,vec3(view.rows[0].z,view.rows[1].z,view.rows[2].z)));
}
fn visibility(origin: vec3<f32>, light: vec3<f32>) -> f32 {
    // Contact occlusion only: offscreen/hidden geometry is unavailable in depth.
    let dims = vec2<f32>(textureDimensions(depth));
    for (var i = 1; i < 9; i++) {
        let q = mix(origin,light,f32(i)/9.0);
        if (q.z >= -view.lens.z) { break; }
        let uv = vec2(q.x/(-q.z*view.lens.x*view.lens.y)*0.5+0.5,
                      0.5-q.y/(-q.z*view.lens.x)*0.5);
        let pixel = vec2<i32>(floor(uv*dims));
        if (!inside(pixel)) { break; }
        let d = textureLoad(depth,pixel,0);
        if (d <= 0.0) { continue; }
        let gap = -q.z - view.lens.z/d;
        // A finite thickness avoids treating every foreground silhouette as an
        // infinitely deep wall. Bias suppresses self-shadowing on the receiver.
        if (gap > 3.0 && gap < 60.0) { return 0.0; }
    }
    return 1.0;
}
@fragment
fn fs_torch(in: VertexOutput) -> @location(0) vec4<f32> {
    let d = textureLoad(depth, vec2<i32>(in.pos.xy), 0);
    if (d <= 0.0) {
        return vec4(0.0);
    }
    let p = vec2<i32>(in.pos.xy);
    if (!inside(p-vec2(1)) || !inside(p+vec2(1))) { return vec4(0.0); }
    let center = position(p,d);
    let dl = textureLoad(depth,p+vec2(-1,0),0);
    let dr = textureLoad(depth,p+vec2(1,0),0);
    let du = textureLoad(depth,p+vec2(0,-1),0);
    let db = textureLoad(depth,p+vec2(0,1),0);
    if (max(dl,dr)<=0.0 || max(du,db)<=0.0) { return vec4(0.0); }
    let left = center-position(p+vec2(-1,0),max(dl,1e-9));
    let right = position(p+vec2(1,0),max(dr,1e-9))-center;
    let up = position(p+vec2(0,-1),max(du,1e-9))-center;
    let down = center-position(p+vec2(0,1),max(db,1e-9));
    let crossNormal = cross(select(right,left,abs(left.z)<abs(right.z)),select(down,up,abs(up.z)<abs(down.z)));
    let normalLength = length(crossNormal);
    if (normalLength < 1e-5) { return vec4(0.0); }
    let normal = crossNormal/normalLength;
    let ndc = vec2(in.uv.x * 2.0 - 1.0, 1.0 - in.uv.y * 2.0);
    let ray = vec3(ndc.x * view.lens.x * view.lens.y, ndc.y * view.lens.x, -1.0);
    let dir = vec3(dot(view.rows[0].xyz, ray), dot(view.rows[1].xyz, ray), dot(view.rows[2].xyz, ray));
    let eye = vec3(view.rows[0].w, view.rows[1].w, view.rows[2].w);
    let world = eye + dir * (view.lens.z / d);
    var glow = vec3(0.0);
    let n = u32(view.count.x);
    for (var i = 0u; i < 16u; i++) {
        if (i >= n) {
            break;
        }
        let light = view.lights[i];
        let falloff = clamp(1.0 - length(world - light.place.xyz) / (light.place.w * radius_scale), 0.0, 1.0);
        if (falloff <= 0.0) { continue; }
        let lightView = to_view(light.place.xyz);
        let delta = lightView-center;
        let facing = max(0.0,dot(normal,delta)/max(length(delta),1e-5));
        if (facing <= 0.0) { continue; }
        let flicker = select(1.0,1.0+0.15*sin(view.time.x*9.0+light.color.w-1.0),light.color.w>0.0);
        glow += light.color.rgb * falloff * falloff * flicker * facing * visibility(center+normal*2.0,lightView);
    }
    return vec4(glow * strength, 0.0);
}
)""";

} // namespace aurora::gfx::post_fx::shaders
