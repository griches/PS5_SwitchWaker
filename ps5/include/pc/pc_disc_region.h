#pragma once
// The disc region is selected once, before any region-dependent game initialization.
// This header deliberately has no game/SDL dependencies (also used by generated assets).
enum class PcDiscRegion { Unsupported, USA, PAL };
PcDiscRegion pc_disc_region();
const char* pc_disc_path();
const char* pc_disc_game_id();
inline bool pc_disc_is_pal() { return pc_disc_region() == PcDiscRegion::PAL; }
// Plain ISO/GCM and CISO header probe; full validation remains in upstream checkDisc.
bool pc_disc_matches_game(const char* path, const char* gameId);
