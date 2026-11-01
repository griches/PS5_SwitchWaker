#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Native-data version of BlueWake haptics.c's shock/quake shaping. No guest
// addresses, platform probing or additional input polling.
struct PcHapticPattern { int index = -1, frame = -99, length = 0, rounds = 0; uint32_t bits = 0; };
struct PcHapticSample { int frame = 0; bool allowed = false; PcHapticPattern shock, quake; };
struct PcHapticLevels { double heavy = 0, light = 0; };
class PcHapticShape {
    bool seen = false, shockOn = false;
    int frame = 0, shockIndex = -1, shockFrame = -99, start = 0, length = 0;
    uint32_t bits = 0;
    double amplitude = 0;
    uint64_t advanced = 0, previous = 0;
    PcHapticLevels levels;
public:
    static bool bit(uint32_t pattern, int size, int k) {
        return k >= 0 && k < size && k < 32 && ((pattern >> (31-k)) & 1u);
    }
    static int count(uint32_t pattern, int size) {
        int n=0;
        for (int k=0; k<size && k<32; ++k) n += bit(pattern,size,k);
        return n;
    }
    PcHapticLevels update(const PcHapticSample& sample, uint64_t now, bool blocked, double strength) {
        if (!seen || frame != sample.frame) {
            if (seen && sample.frame < frame) shockOn = false;
            frame = sample.frame;
            advanced = now;
            seen = true;
        }
        const auto& shock = sample.shock;
        if (shock.index >= 0 && shock.frame >= 0 &&
            (shockIndex < 0 || shockIndex != shock.index || shock.frame < shockFrame)) {
            if (shock.length > 0 && shock.length <= 32) {
                shockOn = true;
                bits = shock.bits;
                length = shock.length;
                start = frame - (shock.frame > 0 ? shock.frame - 1 : -1);
                amplitude = std::clamp(0.4 + 0.075 * count(bits,length), 0.5, 1.0);
            } else shockOn = false;
        }
        shockIndex = shock.index;
        shockFrame = shock.frame;
        if (shock.index < 0 || shock.frame < 0) shockOn = false;
        const auto& quake = sample.quake;
        const bool quaking = quake.index >= 0 && quake.frame >= 0 && quake.length > 0 && quake.length <= 32;
        if (blocked || !sample.allowed || now < advanced || now - advanced > 150000000ull || (!shockOn && !quaking)) {
            levels = {};
            previous = now;
            return {};
        }
        PcHapticLevels target;
        const int k = frame - start;
        if (shockOn && k >= length) shockOn = false;
        if (shockOn && bit(bits,length,k)) {
            target.heavy = amplitude;
            target.light = amplitude * (bit(bits,length,k-1) ? 0.5 : 0.9);
        }
        if (quaking) {
            const double density = std::min(1.0, (count(quake.bits,quake.length) + std::max(0,quake.rounds)) / double(quake.length));
            const double level = std::clamp(0.15 + 0.9 * density, 0.2, 0.75);
            target.heavy = std::max(target.heavy, level * (bit(quake.bits,quake.length,frame % quake.length) ? 1.0 : 0.7));
            target.light = std::max(target.light, level * 0.25);
        }
        const double retraces = previous && now >= previous ? (now-previous) * 59.94 / 1e9 : 1.0;
        previous = now;
        levels.heavy = std::max(target.heavy, levels.heavy * std::pow(0.55,retraces));
        levels.light = std::max(target.light, levels.light * std::pow(0.45,retraces));
        if (levels.heavy < 0.02) levels.heavy = 0;
        if (levels.light < 0.02) levels.light = 0;
        strength = std::isfinite(strength) ? std::clamp(strength,0.0,1.0) : 0;
        return {levels.heavy*strength, levels.light*strength};
    }
};
// Used by the independent output timer, not the game's event pump.
struct PcHapticExpiry {
    uint64_t deadline = 0;
    void arm(uint64_t now) { deadline = now + 120000000ull; }
    bool expired(uint64_t now) {
        if (!deadline || now < deadline) return false;
        deadline = 0;
        return true;
    }
};
#if defined(__PROSPERO__)
void pc_haptics_init();
void pc_haptics_tick();
bool pc_haptics_forward_motor(int port);
#endif
