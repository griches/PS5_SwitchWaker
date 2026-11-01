#include "pc/pc_hd_visuals.h"
#include "post_fx_region.hpp"
#include "post_fx_depth.hpp"
#include <cassert>
#include <limits>

int main() {
    // Finite reversed depth must reconstruct the same surface as the native
    // geometry replayed into the sun map, including distant receivers.
    for (const float n : {1.f, 20.f, 100.f}) for (const float f : {10000.f, 100000.f}) {
        const auto conversion = aurora::gfx::post_fx::depth_conversion(n,f);
        for (const float z : {n, 500.f, 5000.f, f * 0.9f}) {
            const float native = n*f/((f-n)*z) - n/(f-n);
            const float effect = native*conversion[0]+conversion[1];
            assert(std::fabs(n/effect-z) < z*0.0001f);
        }
        assert(conversion[0] > 0 && conversion[1] > 0);
    }
    assert(aurora::gfx::post_fx::depth_conversion(20,10)[0] == 1);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    using aurora::gfx::post_fx::active_region;
    auto region = active_region(1920,1080,0.75f);
    assert(region.width == 1440 && region.height == 810 && region.cropped);
    region = active_region(1920,1080,nan);
    assert(region.width == 1920 && region.height == 1080 && !region.cropped);
    region = active_region(1,1,0.25f);
    assert(region.width == 1 && region.height == 1 && !region.cropped);
    region = active_region(0,0,0.25f);
    assert(region.width == 0 && region.height == 0);
    region = active_region(641,481,0.5f);
    assert(region.width == 321 && region.height == 241);
    // Resizing/scaling must never copy outside the source, even at odd sizes.
    for (unsigned size = 1; size < 2048; size += 17) {
        for (float scale : {-1.f,0.f,0.25f,0.5f,0.75f,1.f,2.f}) {
            region = active_region(size,size+1,scale);
            assert(region.width > 0 && region.width <= size);
            assert(region.height > 0 && region.height <= size+1);
        }
    }

    float inverse[12]{1,0,0,0,0,1,0,0,0,0,1,0};
    assert(pc_hd_camera_valid(60,16.f/9,1,inverse));
    assert(!pc_hd_camera_valid(60,16.f/9,1,nullptr));
    assert(!pc_hd_camera_valid(nan,16.f/9,1,inverse));
    assert(!pc_hd_camera_valid(60,0,1,inverse));
    assert(!pc_hd_camera_valid(60,16.f/9,0,inverse));
    inverse[7] = nan;
    assert(!pc_hd_camera_valid(60,16.f/9,1,inverse));
    for (unsigned stage = 0; stage < 16; ++stage)
        assert(pc_hd_outdoors(stage) == (stage == 0 || stage == 7));

    std::array<PcHdLight,16> lights{};
    unsigned count = 0;
    for (unsigned i = 0; i < 20; ++i) {
        PcHdLight candidate{{float(i),0,0,100,1,0.5f,0,1},float(200-i)};
        pc_hd_insert_light(lights,count,candidate);
    }
    assert(count == 16);
    // Keep the nearest sixteen, even when they arrive after the first sixteen.
    for (const auto& light : lights) {
        assert(light.data[0] >= 4 && light.data[0] <= 19);
        assert(light.distance <= 196);
    }
    const auto before = lights;
    PcHdLight invalid{{0,0,0,100,1,1,1,0},6001};
    pc_hd_insert_light(lights,count,invalid);
    invalid.distance = nan;
    pc_hd_insert_light(lights,count,invalid);
    invalid.distance = 0;
    invalid.data[3] = 0;
    pc_hd_insert_light(lights,count,invalid);
    invalid.data[3] = 100;
    invalid.data[4] = nan;
    pc_hd_insert_light(lights,count,invalid);
    assert(count == 16);
    for (unsigned i = 0; i < count; ++i) {
        assert(lights[i].data == before[i].data);
        assert(lights[i].distance == before[i].distance);
    }
    // Reach is measured from a light's surface; a containing light is valid.
    pc_hd_insert_light(lights,count,{{0,0,0,100,1,1,1,0},-50});
    unsigned containing = 0;
    for (const auto& light : lights) containing += light.distance == -50;
    assert(containing == 1 && count == 16);
}
