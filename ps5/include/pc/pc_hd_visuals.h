#pragma once
#include <algorithm>
#include <array>
#include <cmath>
struct PcHdLight {
    std::array<float,8> data{};
    float distance = 0;
};
inline bool pc_hd_outdoors(unsigned stageType) { return stageType == 0 || stageType == 7; }
inline bool pc_hd_camera_valid(float fovy,float aspect,float nearPlane,const float* inv) {
    if (!inv || !std::isfinite(fovy) || fovy<=1 || fovy>=170 || !std::isfinite(aspect) || aspect<=0.1f ||
        !std::isfinite(nearPlane) || nearPlane<=0) return false;
    for (int i=0;i<12;++i) if (!std::isfinite(inv[i])) return false;
    return true;
}
inline void pc_hd_insert_light(std::array<PcHdLight,16>& lights,unsigned& count,const PcHdLight& candidate) {
    if (!std::isfinite(candidate.distance) || candidate.distance > 6000 ||
        !(candidate.data[3]>1 && candidate.data[3]<10000)) return;
    for (float value:candidate.data) if (!std::isfinite(value)) return;
    if (count < lights.size()) { lights[count++]=candidate; return; }
    unsigned far=0;
    for (unsigned i=1;i<count;++i) if(lights[i].distance>lights[far].distance) far=i;
    if(candidate.distance<lights[far].distance) lights[far]=candidate;
}
#if defined(__PROSPERO__)
struct view_class;
void pc_hd_visuals_init();
void pc_hd_visuals_finish();
void pc_hd_visuals_finish_image();
void pc_hd_visuals_prepare(const view_class* view);
#endif
