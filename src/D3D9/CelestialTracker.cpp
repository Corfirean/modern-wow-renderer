#include "CelestialTracker.h"
#include <wrl/client.h>
#include <cmath>
#include <algorithm>

namespace renderer
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        void UpdateBody(CelestialBody& body, float cx, float cy, float cz,
            const D3DMATRIX& view, const D3DMATRIX& proj, uint64_t frameIndex)
        {
            float len = std::sqrt(cx * cx + cy * cy + cz * cz);
            if (!std::isfinite(len) || len < 1e-4f)
                return;

            Vec3 viewDir{ cx / len, cy / len, cz / len };

            // World-space direction: transform by the inverse of this same
            // draw's VIEW rotation. VIEW is a rigid transform (rotation +
            // translation, no scale), so its inverse rotation is just its
            // transpose - using this draw's own VIEW keeps the conversion
            // self-consistent instead of assuming it numerically matches
            // the separate shader-based view matrix captured elsewhere.
            Vec3 worldDir{
                viewDir.x * view.m[0][0] + viewDir.y * view.m[1][0] + viewDir.z * view.m[2][0],
                viewDir.x * view.m[0][1] + viewDir.y * view.m[1][1] + viewDir.z * view.m[2][1],
                viewDir.x * view.m[0][2] + viewDir.y * view.m[1][2] + viewDir.z * view.m[2][2]
            };
            worldDir = Normalize(worldDir);

            float p00 = proj.m[0][0], p11 = proj.m[1][1];
            if (!(p00 > 0.0f) || !(p11 > 0.0f))
                return;

            body.visible = true;
            body.viewSpaceDirection = viewDir;
            body.worldSpaceDirection = worldDir;
            body.screenX = 0.5f + 0.5f * (cx / cz) * p00;
            body.screenY = 0.5f - 0.5f * (cy / cz) * p11;
            body.lastSeenFrame = frameIndex;
        }
    }

    CelestialTracker& CelestialTracker::Instance()
    {
        static CelestialTracker instance;
        return instance;
    }

    void CelestialTracker::BeginFrame(uint64_t frameIndex)
    {
        m_frameIndex = frameIndex;
        m_sun.visible = false;
        m_moon.visible = false;
    }

    void CelestialTracker::Observe(IDirect3DDevice9* device, UINT primitiveCount)
    {
        if (!device || primitiveCount == 0 || primitiveCount > 8)
            return;

        // Both known patterns are fixed-function - this rejects the vast
        // majority of draw calls with just two cheap state queries.
        ComPtr<IDirect3DVertexShader9> vs;
        if (FAILED(device->GetVertexShader(vs.GetAddressOf())) || vs)
            return;
        ComPtr<IDirect3DPixelShader9> ps;
        if (FAILED(device->GetPixelShader(ps.GetAddressOf())) || ps)
            return;

        DWORD alphaTest = 0;
        device->GetRenderState(D3DRS_ALPHATESTENABLE, &alphaTest);
        if (!alphaTest)
            return;

        ComPtr<IDirect3DBaseTexture9> texture;
        if (FAILED(device->GetTexture(0, texture.GetAddressOf())) || !texture)
            return;

        D3DMATRIX world{}, view{}, proj{};
        if (FAILED(device->GetTransform(D3DTS_WORLD, &world))) return;
        if (FAILED(device->GetTransform(D3DTS_VIEW, &view))) return;
        if (FAILED(device->GetTransform(D3DTS_PROJECTION, &proj))) return;

        float worldScaleSq = world.m[0][0] * world.m[0][0] + world.m[0][1] * world.m[0][1] + world.m[0][2] * world.m[0][2];
        // Widened from the original (0.02 / [6,20]) after in-game testing:
        // near screen edges the billboard's VIEW-space offset shifts more
        // of its magnitude into x/y (wider viewing angle from forward),
        // which can push z below the old lower bound and nudge the
        // translation-zero check for the sun pattern outside its old
        // epsilon. The other three gates (fixed-function, alphaTest,
        // texture bound, plus the WORLD-scale shape) already carry the
        // real discriminating power, so these can afford to be generous.
        bool viewTranslationZero = std::fabs(view.m[3][0]) < 0.05f && std::fabs(view.m[3][1]) < 0.05f && std::fabs(view.m[3][2]) < 0.05f;
        bool worldIsLargeScale = worldScaleSq >= 4.0f;
        bool worldIsIdentityScale = std::fabs(worldScaleSq - 1.0f) < 0.15f;
        float viewTransZ = view.m[3][2];

        // Camera-space position of the billboard's local origin: transform
        // (0,0,0,1) by WORLD then VIEW. For a row-vector convention this is
        // simply the translation row of WORLD carried through VIEW's
        // rotation+translation - equivalent to composing the two matrices
        // and reading the translation row, without needing the full 4x4
        // product.
        float wx = world.m[3][0], wy = world.m[3][1], wz = world.m[3][2];
        float cx = wx * view.m[0][0] + wy * view.m[1][0] + wz * view.m[2][0] + view.m[3][0];
        float cy = wx * view.m[0][1] + wy * view.m[1][1] + wz * view.m[2][1] + view.m[3][1];
        float cz = wx * view.m[0][2] + wy * view.m[1][2] + wz * view.m[2][2] + view.m[3][2];
        if (cz <= 0.05f)
            return;

        if (viewTranslationZero && worldIsLargeScale)
        {
            UpdateBody(m_sun, cx, cy, cz, view, proj, m_frameIndex);
        }
        else if (!viewTranslationZero && worldIsIdentityScale && viewTransZ > 1.0f && viewTransZ < 40.0f)
        {
            UpdateBody(m_moon, cx, cy, cz, view, proj, m_frameIndex);
        }
    }
}
