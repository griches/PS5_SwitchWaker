#pragma once
#include <cstdint>
#include <span>
#include <vector>

// Pure transforms: inputs remain unchanged; output owns its bytes.
bool pc_ui_transform_layout(std::span<const uint8_t> original,
                            std::span<const uint8_t> rules,
                            std::span<const char* const> textures,
                            std::vector<uint8_t>& output);
bool pc_ui_compose_camera(std::span<const uint8_t> original,
                          std::span<const uint8_t> prompt,
                          std::vector<uint8_t>& output);
