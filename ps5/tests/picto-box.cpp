#include "pc/pc_picto_geometry.h"
#include "pc/pc_picto_readback.h"
#include "gfx/pipeline_policy.hpp"
#include <cassert>
#include <cmath>

static bool near(float a, float b, float epsilon = 0.001f) { return std::abs(a-b) < epsilon; }
int main() {
    using namespace pc::picto;
    struct Aspect { float ratio, left, right, viewportX, viewportWidth; };
    for (const auto& a : {Aspect{4.f/3, -9, 650, 0, 640},
                          Aspect{16.f/9, -123, 767, 81.977528f, 473.88764f},
                          Aspect{16.f/10, -77.4f, 720.2f, 54.88465f, 528.78636f}}) {
        const auto v = gallery(a.left, a.right);
        assert(near(v.x, a.viewportX));
        assert(near(v.width, a.viewportWidth));
        // Every picture vertex, including off-centre/animated side photos,
        // must map identically to the corresponding frame's 2D coordinates.
        for (float x : {-280.f, -9.f, 0.f, 320.f, 650.f, 920.f}) {
            const float photo = v.x + (x + 9) * v.width / 659;
            const float frame = (x - a.left) * 640 / (a.right-a.left);
            assert(near(photo, frame));
        }
        const auto copy = sourceSpan(168, 304, a.ratio);
        assert(copy.width > 0);
        assert(near(copy.width / fraction(a.ratio), 304, 0.61f));
        assert(copy.left == sourceX(168, a.ratio));
        // The preview and viewfinder share one projection, retaining native
        // margins after camera-centred narrowing. Preview vertices stay native.
        const auto finder = viewfinder(a.ratio);
        for (float x : {168.f, 174.f, 468.f, 472.f}) {
            const float screen = (x - finder.x) * 640 / finder.width;
            const float nativeScreen = (x + 9) * 640 / 659;
            const float expected = 320 + (nativeScreen - 320) * fraction(a.ratio);
            assert(near(screen, expected));
        }
        // Applying sourceSpan to these vertices as well would squeeze the
        // confirmation image a second time at widescreen aspect ratios.
        const float previewWidth = 304.f * 640.f / finder.width;
        const float nativeWidth = 304.f * 640.f / 659.f;
        assert(near(previewWidth / nativeWidth, fraction(a.ratio)));
        const auto shutter = sourceSpan(174, 294, a.ratio);
        assert(shutter.left >= copy.left && shutter.left + shutter.width <= copy.left + copy.width);
        assert(originalPixels(int(std::lround(2400 * fraction(a.ratio))), a.ratio) == 2400);
    }
    assert(sourceSpan(168,304,4.f/3).left == 168);
    assert(sourceSpan(168,304,4.f/3).width == 304);
    assert(sourceSpan(168,304,16.f/9).left == 206);
    assert(sourceSpan(168,304,16.f/9).width == 228);
    assert(sourceSpan(168,304,16.f/10).width == 253);

    // Actual snapshot sampler used by pc_capture.cpp: pixel centres, scaling,
    // channel order, edges and consecutive successful/failed photographs.
    std::vector<uint8_t> rgba(4*4*4, 0);
    for (unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
        auto* p=rgba.data()+(y*4+x)*4;
        p[0]=uint8_t(x); p[1]=uint8_t(y); p[2]=0xAB; p[3]=uint8_t((y*4+x)*4);
    }
    const auto sample = [&](bool valid, unsigned x, unsigned y) {
        return peekArgb(valid,rgba,4,4,206,76,2,2,x,y);
    };
    assert(sample(true,206,76)==0x140101ABu);
    assert(sample(true,207,77)==0x3C0303ABu);
    assert(sample(false,206,76)==0xFFFFFFFFu); // old bytes cannot become a new subject
    assert(sample(true,205,76)==0xFFFFFFFFu);
    assert(sample(true,208,76)==0xFFFFFFFFu);
    assert(sample(true,206,75)==0xFFFFFFFFu);
    assert(sample(true,206,78)==0xFFFFFFFFu);
    assert(peekArgb(true,rgba,0,4,206,76,2,2,206,76)==0xFFFFFFFFu);
    rgba.pop_back();
    assert(sample(true,206,76)==0xFFFFFFFFu); // truncated/inconsistent readback
    assert(!actorSlot(0xFFFFFFFFu,63));
    assert(!actorSlot(0xFC000000u,63));
    assert(!actorSlot(0,0));
    assert(actorSlot(0,1));
    assert(!actorSlot(0x04000000u,1));
    assert(actorSlot(0x04000000u,2));
    for (bool blocking : {false,true}) for(bool color : {false,true}) for(bool alpha : {false,true}) {
        const bool wait=aurora::gfx::must_wait_for_gx_pipeline(blocking,color,alpha);
        if (blocking || (!color && alpha)) assert(wait);
        else assert(!wait); // ordinary cold draws retain the existing asynchronous path
    }
}
