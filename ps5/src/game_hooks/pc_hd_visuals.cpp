// Typed native inputs for BlueWake's HD post-processing (hd_look.c).
#include "pc/pc_hd_visuals.h"
#if defined(__PROSPERO__)
#include "pc/pc_settings.h"
#include "pc/game_hooks.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_kankyo_wether.h"
#include "f_op/f_op_view.h"
#include <aurora/aurora.h>
#include <SDL3/SDL_timer.h>
#include <cstring>
#include <cstdint>
namespace pc { bool menuOpen(); }
namespace {
enum Effect { Bloom, FarBlur, Clouds, Edge, Torch, Count };
bool enabled[Count]{}, initialized;
int ssaoLevel = 0;
int colourGrade = 0;
bool fxaa = false;
int sharpening = 0;
const char* keys[] = {"COS_HD_BLOOM","COS_HD_FAR_BLUR","COS_HD_CLOUD_SHADOWS",
    "COS_HD_EDGE_LIGHT","COS_HD_TORCH_LIGHT"};
void apply(const char* key,const char* value,void*) {
    if (std::strcmp(key,"COS_FXAA")==0) fxaa = std::strcmp(value,"1")==0;
    if (std::strcmp(key,"COS_SHARPENING")==0)
        sharpening = std::strcmp(value,"2")==0 ? 2 : (std::strcmp(value,"1")==0 ? 1 : 0);
    if (std::strcmp(key,"COS_COLOUR_GRADE")==0)
        colourGrade = std::strcmp(value,"vivid")==0 ? 1 : (std::strcmp(value,"warm")==0 ? 2 : 0);
    if (std::strcmp(key,"COS_HD_SSAO")==0)
        ssaoLevel = std::strcmp(value,"2")==0 ? 2 : (std::strcmp(value,"1")==0 ? 1 : 0);
    for (int i=0;i<Count;++i) if(std::strcmp(key,keys[i])==0) enabled[i]=std::strcmp(value,"1")==0;
}
const PcSettingChoice toggle[]={{"0", "Off"},{"1", "On"}};
const PcSettingChoice ssaoChoices[]={{"0", "Off"},{"1", "Subtle"},{"2", "Strong"}};
const PcSettingChoice gradeChoices[]={{"original", "Original"},{"vivid", "Vivid"},{"warm", "Warm"}};
const PcSettingChoice sharpenChoices[]={{"0", "Off"},{"1", "Low"},{"2", "High"}};
const PcSettingDesc settings[]={
    {"COS_FXAA","Anti-aliasing (FXAA)","Smooths jagged edges before the HUD. Can soften fine texture detail.",PC_SETTING_TAB_GRAPHICS,0,toggle,2,"0",apply,nullptr,78},
    {"COS_SHARPENING","Sharpening","Sharpens the world after upscaling. Does not affect the HUD.",PC_SETTING_TAB_GRAPHICS,0,sharpenChoices,3,"0",apply,nullptr,79},
    {"COS_COLOUR_GRADE","Colour grading","Original keeps the game's colours. Vivid adds richer colour and contrast; Warm adds a warm tint. Does not affect the HUD.",PC_SETTING_TAB_GRAPHICS,0,gradeChoices,3,"original",apply,nullptr,77},
    {"COS_HD_SSAO","Ambient occlusion","Shades nearby corners and creases. Choose Subtle or Strong. Experimental; off by default.",PC_SETTING_TAB_GRAPHICS,0,ssaoChoices,3,"0",apply,nullptr,76},
    {"COS_HD_BLOOM","Soft bloom","Adds a soft glow around bright areas. Does not affect the HUD.",PC_SETTING_TAB_GRAPHICS,0,toggle,2,"0",apply,nullptr,70},
    {"COS_HD_CLOUD_SHADOWS","Cloud shadows","Procedural shadows follow wind, cloud cover and sunlight. Shapes do not match individual clouds.",PC_SETTING_TAB_GRAPHICS,0,toggle,2,"0",apply,nullptr,72},
    {"COS_HD_EDGE_LIGHT","Edge light","Adds a warm glow to sun-facing outlines outdoors.",PC_SETTING_TAB_GRAPHICS,0,toggle,2,"0",apply,nullptr,73},
    {"COS_HD_TORCH_LIGHT","Torch light","Adds surface light near torches and fires.",PC_SETTING_TAB_GRAPHICS,0,toggle,2,"0",apply,nullptr,74},
    {"COS_HD_FAR_BLUR","Distant blur","Adds extra distant blur, separate from depth of field. Does not affect the HUD.",PC_SETTING_TAB_GRAPHICS,0,toggle,2,"0",apply,nullptr,75},
};
}
void pc_hd_visuals_init() {
    if(initialized) return;
    initialized=true;
    for(const auto& setting:settings) {
        pc_settings_register(&setting);
        apply(setting.key,pc_settings_get(setting.key),nullptr);
    }
}
void pc_hd_visuals_finish() {
    const auto* viewport = dComIfGp_getCurrentViewport();
    if (!viewport) return;
    const auto& clip = viewport->mScissor;
    // Pass splitting allocates renderer buffers that must outlive the current scene heap.
    JKRPcHostAllocScope hostAlloc;
    aurora_finish_world_effects(static_cast<int32_t>(clip.mXOrig), static_cast<int32_t>(clip.mYOrig),
                              static_cast<int32_t>(clip.mWidth), static_cast<int32_t>(clip.mHeight));
}
void pc_hd_visuals_finish_image() {
    const auto* viewport = dComIfGp_getCurrentViewport();
    if (!viewport) return;
    const auto& clip = viewport->mScissor;
    JKRPcHostAllocScope hostAlloc;
    aurora_finish_image_effects(static_cast<int32_t>(clip.mXOrig), static_cast<int32_t>(clip.mYOrig),
                              static_cast<int32_t>(clip.mWidth), static_cast<int32_t>(clip.mHeight));
}
void pc_hd_visuals_prepare(const view_class* view) {
    pc_hd_visuals_init();
    float inv[12]{};
    if(view) std::memcpy(inv,view->mInvViewMtx,sizeof(inv));
    float fovy=view?view->mFovy:0, aspect=view?view->mAspect:0, nearPlane=view?view->mNear:0, farPlane=view?view->mFar:0;
    if(view) pc_fps60_render_camera(inv,&fovy,&aspect,&nearPlane,&farPlane);
    const bool valid=view && pc_hd_camera_valid(fovy,aspect,nearPlane,inv) &&
        std::isfinite(farPlane) && farPlane>nearPlane;
    const auto* stage=dComIfGp_getStageStagInfo();
    const bool outdoor=valid && stage && pc_hd_outdoors(dStage_stagInfo_GetSTType(const_cast<stage_stag_info_class*>(stage)));
    aurora_set_ssao(valid ? ssaoLevel : 0);
    aurora_set_colour_grade(valid ? colourGrade : 0);
    aurora_set_image_filters(valid && fxaa, valid ? sharpening : 0);
    aurora_set_bloom(enabled[Bloom]);
    aurora_set_far_blur(valid && enabled[FarBlur]);
    // Integrate wind instead of multiplying it by uptime, so changing wind never
    // jumps the pattern. Keep it still while the options menu pauses the game.
    static uint64_t lastCloudTick = 0;
    static double cloudX = 0., cloudZ = 0.;
    const uint64_t tick = SDL_GetTicksNS();
    const double dt = lastCloudTick && !pc::menuOpen()
        ? std::clamp(double(tick - lastCloudTick) / 1e9, 0., .1) : 0.;
    lastCloudTick = tick;
    const cXyz wind = dKyw_get_wind_vecpow();
    const float weather = std::clamp(g_env_light.mVrkumoStrength, 0.f, 1.f);
    const float speed = 30.f * (4.f + weather * 4.3f);
    if (std::isfinite(wind.x) && std::isfinite(wind.z)) {
        cloudX += wind.x * speed * dt;
        cloudZ += wind.z * speed * dt;
    }
    float coverage = 0.f;
    if (outdoor && g_env_light.mVrkumoStatus == 1 && g_env_light.mpVrkumoPacket) {
        for (const auto& cloud : g_env_light.mpVrkumoPacket->mInst)
            coverage += std::clamp(cloud.mAlpha, 0.f, 1.f);
        coverage = std::clamp(coverage / 50.f, 0.f, 1.f);
    }
    aurora_set_cloud_weather(float(cloudX), float(cloudZ), coverage, 1.f - .75f * weather);
    aurora_set_cloud_shadows(outdoor && enabled[Clouds]);
    aurora_set_edge_light(outdoor && enabled[Edge]);
    aurora_set_torch_light(valid && enabled[Torch]);
    if(!valid) { aurora_set_point_lights(nullptr,0); return; }
    aurora_set_world_view(inv,std::tan(fovy * 0.5f * 3.14159265f/180),aspect,nearPlane,0,
        float(SDL_GetTicksNS()/1e9),farPlane);
    // The game positions the distant sun around its logical eye. Preserve that
    // direction when the rendered eye is interpolated between logic updates.
    aurora_set_sun_direction(g_env_light.mSunPos.x-view->mInvViewMtx[0][3],
        g_env_light.mSunPos.y-view->mInvViewMtx[1][3],g_env_light.mSunPos.z-view->mInvViewMtx[2][3]);
    std::array<PcHdLight,16> found{};
    unsigned count=0;
    if(enabled[Torch]) for(const auto* light:g_env_light.mpPLights) {
        if(!light) continue;
        const float dx=light->mPos.x-inv[3],dy=light->mPos.y-inv[7],dz=light->mPos.z-inv[11];
        PcHdLight candidate{{light->mPos.x,light->mPos.y,light->mPos.z,light->mPower,
            std::clamp(light->mColor.r/255.f,0.f,1.f),std::clamp(light->mColor.g/255.f,0.f,1.f),
            std::clamp(light->mColor.b/255.f,0.f,1.f),light->mFluctuation != 0 ? 1.f + float((reinterpret_cast<uintptr_t>(light) >> 4) & 65535) * (6.2831853f / 65536.f) : 0.f},
            std::sqrt(dx*dx+dy*dy+dz*dz)-light->mPower};
        pc_hd_insert_light(found,count,candidate);
    }
    float packed[16*8]{};
    for(unsigned i=0;i<count;++i) std::memcpy(packed+i*8,found[i].data.data(),8*sizeof(float));
    aurora_set_point_lights(packed,count);
}
#endif
