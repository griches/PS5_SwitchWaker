#pragma once
#include <cstdint>

// Draw-only presentation. Upstream owns scheduling, counts and completion.
void pc_ps5_shader_loading(uint32_t done, uint32_t total, double secondsRemaining);
