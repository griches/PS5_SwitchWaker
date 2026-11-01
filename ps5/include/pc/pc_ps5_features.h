#pragma once
// Explicit startup registration; game thread, after Aurora exists, before menu builtins.
void pc_ps5_features_init();

extern "C" const char* ps5_system_language();
