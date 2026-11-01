#!/usr/bin/env python3
"""Check the native/WGSL uniform ABI without a compiler or GPU."""
import ast
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'ps5/aurora/gfx/post_fx.cpp').read_text()
source += (ROOT / 'ps5/aurora/gfx/post_fx_shaders.hpp').read_text()
header = (ROOT / 'ps5/aurora/gfx/post_fx.hpp').read_text()


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def record_layout(shader, name):
    records = dict(re.findall(r'struct (\w+)\s*\{([^}]+)\}', shader))

    def layout(kind):
        if kind in ('f32', 'u32', 'i32'):
            return 4, 4
        vector = re.fullmatch(r'vec([234])<f32>', kind)
        if vector:
            count = int(vector[1])
            return (8 if count == 2 else 16), count * 4
        array = re.fullmatch(r'array<(.+),\s*(\d+)>', kind)
        if array:
            alignment, size = layout(array[1].strip())
            # These uniform arrays must meet the uniform address-space's
            # 16-byte alignment rule, in addition to the element alignment.
            alignment = max(16, alignment)
            return alignment, align(size, alignment) * int(array[2])
        offset, alignment = 0, 16
        for line in records[kind].splitlines():
            line = line.split('//', 1)[0].strip().rstrip(',')
            if not line:
                continue
            _, field_type = line.split(':', 1)
            field_alignment, size = layout(field_type.strip())
            offset = align(offset, field_alignment) + size
            alignment = max(alignment, field_alignment)
        return alignment, align(offset, alignment)

    return layout(name)[1]


camera_floats = int(re.search(r'std::array<float, (\d+)> view', header)[1])
lights = int(re.search(r'kMaxLights = (\d+)', header)[1])
expression = re.search(r'kTorchUniformSize = ([^;]+);', source)[1]
expression = expression.replace('sizeof(float)', '4').replace('kMaxLights', str(lights))


def constant(node):
    if isinstance(node, ast.Constant) and isinstance(node.value, int):
        return node.value
    assert isinstance(node, ast.BinOp)
    left, right = constant(node.left), constant(node.right)
    if isinstance(node.op, ast.Add):
        return left + right
    assert isinstance(node.op, ast.Mult)
    return left * right


torch_bytes = constant(ast.parse(expression, mode='eval').body)
expected = {'kSsaoShader': (camera_floats + 4) * 4,
            'kCloudShader': (camera_floats + 4) * 4,
            'kEdgeShader': (camera_floats + 4) * 4,
            'kTorchShader': torch_bytes}
for name, host_bytes in expected.items():
    shader = re.search(name + r' = R"""\((.*?)\)""";', source, re.S)[1]
    shader_bytes = record_layout(shader, 'View')
    assert shader_bytes == host_bytes, (name, shader_bytes, host_bytes)
print('Visual uniform ABI passed: SSAO/cloud/edge 96 B, point lights 624 B.')

# The game calls the public C API, not the namespaced implementation. A backend
# setter without its forwarding definition compiles but fails at the title link.
game_hooks = (ROOT / 'ps5/src/game_hooks/pc_hd_visuals.cpp').read_text()
used_api = set(re.findall(r'\b(aurora_set_\w+)\s*\(', game_hooks))
public_definitions = set(re.findall(r'^void (aurora_set_\w+)\s*\([^)]*\)\s*\{', source, re.M))
missing_api = used_api - public_definitions
assert not missing_api, f'Missing visual C API definitions: {sorted(missing_api)}'
print(f'Visual C API passed: all {len(used_api)} game-hook setters have public definitions.')

# Every world composite must respect cutscene bars, including the two blur paths.
assert 'true, scissor, crop_uniform(size, scissor));' in source
assert 'g_view[0], dst, true, resolve, nullptr, &scissor, bounds);' in source
assert 'pass.SetScissorRect(scissor[0], scissor[1], scissor[2], scissor[3]);' in source
# Scratch targets are allocated separately so a non-blur effect needs neither set.
assert 'if (state.bloom) allocate(g_tex, g_view,' in source
assert 'g_farTex' not in source and 'g_farSourceView' in source
for effect, pipeline in [('torch', 'g_torchPass'), ('edge', 'g_edgePass'), ('clouds', 'g_cloudPass')]:
    assert f'if (state.{effect} && (!{pipeline}' in source

assert 'camera.cloudWeather = g_cloudWeather;' in source
assert 'uniform.begin() + 20' in source
assert '(world.xz - view.weather.xy) / scale' in source
assert 'smoothstep(0.02, 0.30, view.time.z)' in source
assert 'vec2(0.6, 0.25)' not in source

assert '.minBindingSize = sizeof(g_camera)' not in source
assert 'cloud.mAlpha' in game_hooks and 'dKyw_get_wind_vecpow()' in game_hooks
assert 'cloudX += wind.x * speed * dt;' in game_hooks
assert '!pc::menuOpen()' in game_hooks
# The shader fades sunlight smoothly, and zero coverage/sunlight never darkens.
def smoothstep(lo, hi, value):
    t = max(0., min(1., (value - lo) / (hi - lo)))
    return t * t * (3. - 2. * t)
for elevation in (-1., 0., .02):
    assert smoothstep(.02, .30, elevation) == 0.
assert smoothstep(.02, .30, .16) < smoothstep(.02, .30, .30) == 1.

assert source.count('state.clouds || state.ssao;') == 2
assert '.ssao = webgpu::g_hasCoreFeatures ? g_ssao.load(std::memory_order_relaxed) : 0' in source
assert 'clouds || edge || torch || ssao || colourGrade' in header
assert 'g_ssaoPass = make_ssao_pipeline(format, samples);' in source
assert 'g_ssaoModule = {};' in source
assert 'camera.valid && effectDepth' in source
assert 'if (!inside(q)' in source
assert 'distance >= radius' in source and '0.18 * view.time.x' in source
assert '"COS_HD_SSAO"' in game_hooks and 'ssaoChoices,3,"0",apply,nullptr,76' in game_hooks

assert '.textureView = g_farView[0]' not in source
assert 'var unused: texture_2d' not in source
# Geometric invariants for the contact term: a plane cannot occlude itself;
# an out-of-range neighbour cannot create a broad silhouette shadow.
def contact(normal, delta):
    distance = sum(v*v for v in delta) ** .5
    if distance <= .001 or distance >= 35.: return 0.
    facing = max(0., (sum(a*b for a,b in zip(normal,delta)) - 1.) / distance - .08)
    return facing * (1. - distance/35.)**2
assert contact((0,0,1), (12,0,0)) == 0.
assert contact((0,0,1), (12,0,10)) > 0.
assert contact((0,0,1), (12,0,-10)) == 0.
assert contact((0,0,1), (0,0,36)) == 0.

# Grading reads a copy of the completed world, never its own render attachment.
assert 'if (!state.image()' in source
assert 'cmd.CopyTextureToTexture(&source, &target, &size);' in source
assert 'draw(cmd,label,pipeline,input,last ? dst : output' in source
assert 'encode_colour_grade(' not in source
assert '.colourGrade = webgpu::g_hasCoreFeatures ? g_colourGrade.load' in source
assert 'gradeChoices,3,"original",apply' in game_hooks
assert 'g_gradePipelines = {};' in source and 'g_imageSource = {};' in source
# The preset curve must preserve endpoints, stay in gamut and avoid crushing
# neutral ramps. Saturated colours should not clip when saturation is raised.
def grade(rgb, saturation, warmth, contrast_amount):
    contrast = [c + contrast_amount*c*(1-c)*(2*c-1) for c in rgb]
    luma = sum(c*w for c,w in zip(contrast,(.2126,.7152,.0722)))
    delta = [c-luma for c in contrast]
    scale = saturation
    for d in delta:
        if d > .00001: scale = min(scale,(1-luma)/d)
        if d < -.00001: scale = min(scale,-luma/d)
    colour = [luma+d*scale for d in delta]
    return [c+warmth*c*(1-c)*w for c,w in zip(colour,(1,.15,-1))]
presets = [tuple(map(float, values)) for values in re.findall(
    r'grade\(c.rgb, ([0-9.]+), ([0-9.]+), ([0-9.]+)\)', source)]
assert len(presets) == 2
for contrast_amount,saturation,warmth in presets:
    assert grade((0,0,0),saturation,warmth,contrast_amount) == [0,0,0]
    assert grade((1,1,1),saturation,warmth,contrast_amount) == [1,1,1]
    for r in range(11):
        for g in range(11):
            for b in range(11):
                assert all(-1e-6 <= c <= 1+1e-6 for c in grade((r/10,g/10,b/10),saturation,warmth,contrast_amount))
    ramp = [grade((i/100,)*3,saturation,warmth,contrast_amount) for i in range(101)]
    assert all(all(a < b for a,b in zip(lo,hi)) for lo,hi in zip(ramp,ramp[1:]))
print('Colour grading checks passed: neutral endpoints, monotonic ramps, gamut and scene-copy wiring.')

# Validate texture/sampler references per module, not across the concatenated
# header: an accidental grading copy in the cloud module crashed at startup.
modules = dict(re.findall(r'inline constexpr const char\* (\w+) = R"""\((.*?)\)""";', source, re.S))
for name, shader in modules.items():
    shader = re.sub(r'//[^\n]*', '', shader)
    declarations = set(re.findall(r'\bvar(?:<[^>]+>)?\s+(\w+)\s*:', shader))
    for texture in re.findall(r'\btexture(?:Sample(?:Level)?|Load|Dimensions|NumSamples)\s*\(\s*(\w+)', shader):
        assert texture in declarations, (name, 'undeclared texture', texture)
    for sampler in re.findall(r'\btextureSample(?:Level)?\s*\(\s*\w+\s*,\s*(\w+)', shader):
        assert sampler in declarations, (name, 'undeclared sampler', sampler)
    if name != 'kGradeShader':
        assert 'fn grade(' not in shader and 'fn fs_grade_' not in shader, name
assert modules['kGradeShader'].count('fn grade(') == 1
print('Shader module checks passed: grading isolation and local texture/sampler declarations.')

# Presets must visibly differ on midtones while retaining smooth shadow ramps.
vivid, warm = presets
v = grade((.3,.5,.7), vivid[1], vivid[2], vivid[0])
assert max(abs(a-b) for a,b in zip(v,(.3,.5,.7))) > .04
w = grade((.5,.5,.5), warm[1], warm[2], warm[0])
assert w[0] - w[2] > .1

assert record_layout(modules['kImageFilterShader'], 'FilterParams') == 32
assert 'if (!state.image()' in source
assert '.fxaa = (filters & 1) != 0, .sharpening = filters >> 1' in source
assert 'encode_image_filters(cmd,g_croppedScene,active' not in source
assert 'g_filterPipelines = {};' in source and 'g_filterModule = {};' in source
assert 'clamp(p, low, high)' in modules['kImageFilterShader']
assert 'return vec4(result, center.a)' in modules['kImageFilterShader']
assert 'let radius = params.strength.y;' in modules['kImageFilterShader']
assert 'let allowance = min(vec3(params.strength.z), (high-low)*0.25);' in modules['kImageFilterShader']
# Peaks must actually sharpen, while flat fields and black/white endpoints
# remain unchanged. Limit ringing to the preset's small absolute excursion.
def sharpen(center, neighbours, strength, limit):
    low,high = min(center,*neighbours),max(center,*neighbours)
    allowance=min(limit,(high-low)*.25)
    boost=min(allowance,max(-allowance,(center-sum(neighbours)/4)*strength))
    return min(1,max(0,center+boost))
for strength,limit in ((.6,.025),(1.2,.05)):
    for i in range(11):
        c=i/10
        assert abs(sharpen(c,[c]*4,strength,limit)-c)<1e-9
        for neighbours in ([0,0,0,0],[1,1,1,1],[0,1,0,1],[.2,.4,.6,.8]):
            result=sharpen(c,neighbours,strength,limit)
            assert 0<=result<=1 and abs(result-c)<=limit+1e-9
    assert sharpen(0,[1]*4,strength,limit)==0
    assert sharpen(1,[0]*4,strength,limit)==1
low=sharpen(.6,[.4]*4,.6,.025)
high=sharpen(.6,[.4]*4,1.2,.05)
assert high > low > .6
assert .6-sharpen(.6,[.8]*4,1.2,.05) > .04
print('Image filter checks passed: bindings, bounds, flat fields, visible extrema response and bounded contrast boost.')

# One colour copy for the complete filter chain, independent of enabled combination.
chain = source.split('void encode_image_filters(',1)[1].split('// Native dynamic resolution',1)[0]
assert chain.count('CopyTextureToTexture') == 1
assert 'std::swap(input,output)' in chain
assert 'passSamples = last ? samples : 1' in chain
for fxaa in (False,True):
    for sharp in (False,True):
        for grade_on in (False,True):
            stages=[x for x,on in enumerate((fxaa,sharp,grade_on)) if on]
            incoming,outgoing='source','work'
            for i,stage in enumerate(stages):
                destination='EFB' if i==len(stages)-1 else outgoing
                assert incoming != destination
                if destination!='EFB': incoming,outgoing=outgoing,incoming
# Explicit game boundary after upscale and before HUD; once per rendered frame.
# These entry points split passes outside the normal Aurora frame scope. Keep
# their command buffers off the small, temporary file-select/game scene heaps.
for hook, boundary in (('pc_hd_visuals_finish', 'aurora_finish_world_effects'),
                       ('pc_hd_visuals_finish_image', 'aurora_finish_image_effects')):
    body = game_hooks.split('void ' + hook + '() {', 1)[1].split('\n}', 1)[0]
    assert body.index('JKRPcHostAllocScope hostAlloc;') < body.index(boundary + '(')
graphic=(ROOT/'vendor/switchwaker/game/src/m_Do/m_Do_graphic.cpp').read_text()
assert graphic.index('pc_dynres_3d_end();') < graphic.index('pc_hd_visuals_finish_image();')
encoding=(ROOT/'.deps/aurora/lib/gfx/encoding.cpp').read_text()
assert 'passInfo.applyImageEffects && !frame.imageEffectsApplied' in encoding
assert 'post_fx::encode_image_filters(cmd, passInfo.copySourceTexture' in encoding
assert 'if (!inside(q)) { continue; }' in modules['kEdgeShader']
assert record_layout(modules['kShader'],'ImageBounds')==32
assert record_layout(modules['kFarShader'],'ImageBounds')==32
assert 'sample_colour(' in modules['kShader'] and 'clamp(uv, lo, hi)' in modules['kShader']
assert 'g_farSourceView, effectDepth' in source
# A foreground/background step must not mix colours; equal-depth colour survives.
def bilateral_weight(d,sd):
    if (d<=0)!=(sd<=0): return 0.
    delta=abs(sd-d)/max(sd,d,1e-9)
    return 1-smoothstep(.02,.08,delta)
assert bilateral_weight(.1,.1)==1
assert bilateral_weight(.1,.5)==0
assert bilateral_weight(0,.1)==bilateral_weight(.1,0)==0
assert bilateral_weight(0,0)==1
assert 'textureLoad(blurred,q,0)' in modules['kFarShader']
# Torch phase belongs to the light, not its position in the packed array.
assert 'reinterpret_cast<uintptr_t>(light)' in game_hooks
assert 'f32(i) * 1.7' not in modules['kTorchShader']
assert 'visibility(center+normal*2.0,lightView)' in modules['kTorchShader']
assert 'if (facing <= 0.0)' in modules['kTorchShader']
assert 'uniform.data() + 28' in source
print('Post-processing regression checks passed: crop sampling, depth separation, filter chain and post-upscale boundary.')
