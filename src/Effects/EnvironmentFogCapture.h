#pragma once
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include "../D3D9/TrackedRenderState.h"

// Read-only capture of WoW's own authored fog for the current draw. Never
// writes any shader state - this observes the native, unmodified vertex
// shader constant at the same register DistanceFog.h already confirmed
// for each shader hash (captured from real shader instructions, not
// guessed), so it must run BEFORE distancefog::Scope or watereffect::Scope
// touch anything for this draw.
//
// The goal (ROUND 5, Phase 11-19): stop treating our atmosphere as one
// universal synthetic profile for the whole game and instead derive its
// distance/colour baseline from whatever WoW itself authored for the
// current zone/time, then layer height fog, ground mist, noise and
// celestial scattering on top of that baseline. Elwynn should still look
// like Elwynn.
namespace renderer {

struct EnvironmentFogState {
    bool valid = false;
    // Distance (world units) at which WoW's own fog curve starts departing
    // from "no fog" and where it reaches "fully fogged".
    float startDistance = 60.f;
    float endDistance = 400.f;
    // Proxy for how sharply the native curve transitions (its pow()
    // exponent) - not a physical density, just carried through.
    float densityHint = 1.f;
    // Only populated from the water shader case (see Observe) - the one
    // shader family where this register has been confirmed reliable this
    // whole session (WaterEffect's deepTint/environment terms already
    // depend on reading it correctly).
    float ambientFogColor[3] = { .36f, .46f, .58f };
    bool colorValid = false;
};

class EnvironmentFogCapture {
public:
    static EnvironmentFogCapture& Instance() { static EnvironmentFogCapture instance; return instance; }

    void Observe(IDirect3DDevice9* d) noexcept {
        try {
            uint64_t vsHash = renderer::g_trackedState.vsHash;
            if (!vsHash) return;
            UINT reg;
            // Same table as DistanceFog.h - registers verified from
            // captured shader instructions for these specific hashes.
            switch (vsHash) {
            case 0xb598fbc887a39675ull: case 0x512e5caf066afb4bull: case 0xbd0060eb34ea4a7cull: reg = 12; break;
            case 0x206d861fd0a721ddull: reg = 4; break;
            case 0xd8f18d8080e6bb3aull: reg = 8; break;
            case 0x17a93e12ce3e0fd1ull: case 0x2361bbcb97cae46ull: case 0x2e3dc5694f4a01afull:
            case 0x5f2c6b3af8c6e543ull: case 0xd6dc2873a70922e9ull: case 0xf6a1996937ab5dc7ull: case 0xff32338728131932ull: reg = 30; break;
            default: return;
            }
            float raw[4]{};
            if (FAILED(d->GetVertexShaderConstantF(reg, raw, 1))) return;
            for (float v : raw) if (!std::isfinite(v)) return;
            // Same shape guard DistanceFog.h uses to confirm this register
            // actually holds the fog curve on this draw, not something else.
            if (raw[0] > 0 || raw[1] < 0 || raw[2] <= 0) return;

            // Invert fog(p.z) = min(pow(max(p.z*x+y,0),z),1): fog==1 (no
            // fog) holds while p.z*x+y>=1, fog reaches 0 (fully fogged) at
            // p.z = -y/x.
            float x = raw[0], y = raw[1];
            if (x >= -1e-6f) return;
            float end = -y / x;
            if (!std::isfinite(end) || end <= 1.f) return;
            float start = std::clamp((1.f - y) / x, 0.f, end);

            m_state.startDistance = start;
            m_state.endDistance = end;
            m_state.densityHint = std::clamp(raw[2], .1f, 8.f);
            m_state.valid = true;

            if (vsHash == 0x206d861fd0a721ddull) {
                float color[4]{};
                if (SUCCEEDED(d->GetPixelShaderConstantF(4, color, 1))) {
                    bool plausible = true;
                    for (int i = 0; i < 3; ++i) if (!std::isfinite(color[i]) || color[i] < 0.f || color[i] > 4.f) plausible = false;
                    if (plausible) {
                        m_state.ambientFogColor[0] = color[0];
                        m_state.ambientFogColor[1] = color[1];
                        m_state.ambientFogColor[2] = color[2];
                        m_state.colorValid = true;
                    }
                }
            }
        } catch (...) {}
    }

    const EnvironmentFogState& State() const { return m_state; }

private:
    EnvironmentFogCapture() = default;
    EnvironmentFogState m_state;
};

}
