#include "pc/pc_ui_transform.h"
#include <array>
#include <cassert>
#include <cstring>

namespace {
void put32(uint8_t* p, uint32_t v) {
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
void put16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
}
int main() {
    // Synthetic BLO: a pane with optional rotation and one texture reference.
    std::vector<uint8_t> original(32 + 52 + 8);
    std::memcpy(original.data(), "SCRNblo1", 8);
    put32(original.data() + 8, original.size());
    auto* pane = original.data() + 32;
    std::memcpy(pane, "PIC1", 4); put32(pane + 4, 52);
    pane[8] = 7; std::memcpy(pane + 12, "cent", 4);
    put16(pane + 24, 90); pane[28] = 4;
    pane[29] = 2; pane[30] = 11; std::memcpy(pane + 31, "font_00.bti", 11);
    pane[42] = 0; pane[43] = 0; pane[44] = 7; pane[45] = 3;
    std::memcpy(pane + 52, "EXT1", 4); put32(pane + 56, 8);
    const auto before = original;
    std::vector<uint8_t> recipe(96);
    std::memcpy(recipe.data(), "P5UILAY1", 8); put32(recipe.data() + 8, 1);
    std::memcpy(recipe.data() + 32, "cent", 4);
    put16(recipe.data() + 36, 560); put16(recipe.data() + 38, 75);
    put16(recipe.data() + 40, 40); put16(recipe.data() + 42, 40);
    recipe[44] = 3; std::memcpy(recipe.data() + 48, "ba_base_04.bti", 14);
    const char* names[] = {"font_00.bti"};
    std::vector<uint8_t> output;
    assert(pc_ui_transform_layout(original, recipe, names, output));
    assert(original == before);
    const auto* edited = output.data() + 32;
    assert(edited[16] == 2 && edited[17] == 48 && edited[19] == 75);
    assert(edited[24] == 0 && edited[25] == 0 && edited[28] == 11);
    assert(edited[30] == 14 && !std::memcmp(edited + 31, "ba_base_04.bti", 14));
    assert(edited[47] == 7 && edited[48] == 3 && edited[49] == 5);
    assert(!std::memcmp(output.data() + output.size() - 8, "EXT1", 4));
    auto malformed = original; put32(malformed.data() + 36, 10000);
    assert(!pc_ui_transform_layout(malformed, recipe, names, output));
    malformed = original; malformed[62] = 255;
    assert(!pc_ui_transform_layout(malformed, recipe, names, output));
    recipe[44] = 4;
    assert(!pc_ui_transform_layout(original, recipe, names, output));

    // Synthetic CI8 source: one opaque white eye pixel at x56, y0.
    // Its visible row must be centered at y15 in the composed RGB5A3 texture.
    std::vector<uint8_t> eye(32 + 4096 + 4);
    eye[0] = 9; put16(eye.data() + 2, 128); put16(eye.data() + 4, 32);
    eye[9] = 2; put16(eye.data() + 10, 2);
    put32(eye.data() + 12, 4128); put32(eye.data() + 28, 32);
    eye[32 + 7 * 32] = 1;
    put16(eye.data() + 4130, 0xffff);
    std::vector<uint8_t> prompt(32 + 128 * 32 * 2);
    prompt[0] = 5; put16(prompt.data() + 2, 128); put16(prompt.data() + 4, 32);
    put32(prompt.data() + 28, 32);
    prompt[32] = 0xab; prompt[33] = 0xcd; // R3 pixels must survive unchanged.
    const auto eyeBefore = eye;
    assert(pc_ui_compose_camera(eye, prompt, output));
    assert(eye == eyeBefore && output[32] == 0xab && output[33] == 0xcd);
    const size_t pixel = 32 + ((15 / 4) * 32 + 56 / 4) * 32 + ((15 % 4) * 4) * 2;
    assert(output[pixel] == 0xff && output[pixel + 1] == 0xff);
    eye[32 + 7 * 32] = 2; // Invalid palette index must not read past the palette.
    assert(!pc_ui_compose_camera(eye, prompt, output));
    eye = eyeBefore; eye.resize(40);
    assert(!pc_ui_compose_camera(eye, prompt, output));
}
