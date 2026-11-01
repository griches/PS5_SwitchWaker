#pragma once
namespace aurora::gfx {
// Alpha-only GX draws encode CPU-visible IDs as well as ordinary alpha clears.
// Their first cold-cache execution must not be dropped.
constexpr bool must_wait_for_gx_pipeline(bool blocking, bool colorUpdate, bool alphaUpdate) {
  return blocking || (!colorUpdate && alphaUpdate);
}
} // namespace aurora::gfx
