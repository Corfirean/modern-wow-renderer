#include "CameraCapture.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace renderer
{
    CameraCapture& CameraCapture::Instance()
    {
        static CameraCapture instance;
        return instance;
    }

    bool CameraCapture::Capture(
        IDirect3DDevice9* device,
        FrameContext& frameContext,
        const CameraCaptureConfig& config,
        float outConstants[13][4],
        float outCapturedViewTranslation[3],
        bool& outCapturedViewValid,
        uint64_t shaderHash)
    {
        if (!device)
            return false;

        m_cameraCaptureShaderHash = shaderHash;

        float v[27][4]{};
        D3DVIEWPORT9 vp{};

        if (FAILED(device->GetVertexShaderConstantF(0, v[0], 27)) ||
            FAILED(device->GetViewport(&vp)) ||
            vp.MinZ != 0.0f || vp.MaxZ <= 0.0f)
            return false;

        for (auto& row : v)
            for (float f : row)
                if (!std::isfinite(f))
                    return false;

        // Verify standard WoW perspective projection and rigid world-view
        if (v[4][0] <= 0.0f || v[5][1] <= 0.0f || v[6][2] <= 1.0f || v[7][2] >= 0.0f ||
            fabsf(v[6][3] - 1.0f) > 0.001f || fabsf(v[7][3]) > 0.001f)
            return false;

        // Verify orthogonality of rotation sub-matrix (v[0..2])
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                float dot = 0.0f;
                for (int k = 0; k < 3; ++k)
                    dot += v[i][k] * v[j][k];
                if (fabsf(dot - (i == j ? 1.0f : 0.0f)) > 0.002f)
                    return false;
            }
        }

        memset(outConstants, 0, sizeof(float) * 13 * 4);

        // c0: Projection unpacking factors [P22, P32, P00, P11]
        outConstants[0][0] = v[6][2];
        outConstants[0][1] = v[7][2];
        outConstants[0][2] = v[4][0];
        outConstants[0][3] = v[5][1];

        // c1: Fog parameters
        outConstants[1][0] = config.baseHeight;
        outConstants[1][1] = config.falloff;
        outConstants[1][2] = config.density;
        outConstants[1][3] = 350.0f;

        float directLuminance = v[26][0] * 0.2126f + v[26][1] * 0.7152f + v[26][2] * 0.0722f;

        float daylightBrightness = std::clamp((directLuminance - 0.18f) * 4.0f, 0.0f, 1.0f);
        float daylightTint = std::clamp((v[26][0] - v[26][2]) * 4.0f + 0.35f, 0.0f, 1.0f);
        float daylight = daylightBrightness * daylightTint;

        float moonBrightness = std::clamp((directLuminance - 0.12f) * 4.0f, 0.0f, 1.0f);
        float moonTint = std::clamp((v[26][2] - v[26][0]) * 6.0f, 0.0f, 1.0f);
        float moonlight = moonBrightness * moonTint * (1.0f - daylight);

        float celestialShadowLight = std::clamp((directLuminance - 0.08f) * 3.8f, 0.18f, 1.0f);

        outConstants[2][0] = -1.0f;
        outConstants[2][1] = -1.0f;
        outConstants[2][2] = 0.0f;
        outConstants[2][3] = vp.MaxZ;

        float lightLen = std::sqrt(v[24][0] * v[24][0] + v[24][1] * v[24][1] + v[24][2] * v[24][2]);
        if (lightLen > 1e-4f)
        {
            // View-space light vector points towards celestial body:
            // In WoW, v[24] is light propagation (towards camera/down).
            // Facing the sun/moon is in direction (-v[24][0], -v[24][1], +v[24][2]).
            float lx = -v[24][0] / lightLen;
            float ly = -v[24][1] / lightLen;
            float lz =  v[24][2] / lightLen;
            outConstants[10][0] = lx;
            outConstants[10][1] = ly;
            outConstants[10][2] = lz;
            outConstants[10][3] = 0.0f;

            if (lz > 0.02f)
            {
                float projX = (lx * v[4][0]) / lz;
                float projY = (ly * v[5][1]) / lz;
                float targetX = 0.5f + 0.5f * projX + config.sunOffsetX;
                float targetY = 0.5f - 0.5f * projY * config.sunVerticalScale + config.sunOffsetY;
                outConstants[2][0] = targetX;
                outConstants[2][1] = targetY;

                float facing = std::clamp((lz + 0.05f) * 2.5f, 0.0f, 1.0f);
                if (targetX < -0.50f || targetX > 1.50f || targetY < -0.50f || targetY > 1.50f)
                    facing = 0.0f;

                outConstants[2][2] = config.strength * (daylight + config.moonStrength * moonlight) * facing;

                frameContext.sunScreenX = targetX;
                frameContext.sunScreenY = targetY;
                frameContext.sunStrength = outConstants[2][2];
            }
            else
            {
                frameContext.sunScreenX = -1.0f;
                frameContext.sunScreenY = -1.0f;
                frameContext.sunStrength = 0.0f;
            }

            frameContext.sunDirectionView = { lx, ly, lz };
        }
        else
        {
            outConstants[10][0] = 0.0f;
            outConstants[10][1] = 0.0f;
            outConstants[10][2] = 1.0f;
            outConstants[10][3] = 0.0f;
            frameContext.sunScreenX = -1.0f;
            frameContext.sunScreenY = -1.0f;
            frameContext.sunStrength = 0.0f;
        }

        // Ambient / horizon color
        for (int i = 0; i < 3; ++i)
        {
            float horizon = v[25][i] * 1.5f + v[26][i] * 0.30f;
            outConstants[3][i] = std::clamp(horizon, 0.12f, 0.95f);
        }

        // c4..c6: Inverse view rotation
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                outConstants[4 + i][j] = v[j][i];

        // c7: Camera world position
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                outConstants[7][i] -= v[3][j] * v[i][j];
        outConstants[7][3] = 1.0f;

        // c8: Fog wash and debug
        outConstants[8][0] = 3.0f;
        outConstants[8][1] = config.fogWash;
        outConstants[8][2] = config.shaftDebug ? 1.0f : 0.0f;

        // View translation
        outCapturedViewTranslation[0] = v[3][0];
        outCapturedViewTranslation[1] = v[3][1];
        outCapturedViewTranslation[2] = v[3][2];
        outCapturedViewValid = true;

        // c9: Time and variation
        outConstants[9][0] = float(GetTickCount64() % 1200000) * 0.001f;
        outConstants[9][1] = 0.018f;
        outConstants[9][2] = config.variation;
        outConstants[9][3] = config.lowLayer;

        // c11: Direct light color
        for (int i = 0; i < 3; ++i)
            outConstants[11][i] = v[26][i];

        // c12: Viewport and ray params
        outConstants[12][0] = 1.0f / static_cast<float>(vp.Width);
        outConstants[12][1] = 1.0f / static_cast<float>(vp.Height);
        outConstants[12][2] = config.raySoftness;
        outConstants[12][3] = config.rayFalloff;

        // Populate FrameContext
        frameContext.cameraPosition = { outConstants[7][0], outConstants[7][1], outConstants[7][2] };
        frameContext.daylightFactor = daylight;
        frameContext.moonlightFactor = moonlight;
        frameContext.shadowLightFactor = celestialShadowLight;
        frameContext.directionalLightColor = { v[26][0], v[26][1], v[26][2] };
        frameContext.viewport = vp;
        frameContext.width = vp.Width;
        frameContext.height = vp.Height;
        frameContext.cameraValid = true;

        frameContext.projUnpack[0] = outConstants[0][0]; // P22
        frameContext.projUnpack[1] = outConstants[0][1]; // P32
        frameContext.projUnpack[2] = outConstants[0][2]; // P00
        frameContext.projUnpack[3] = outConstants[0][3]; // P11
        frameContext.depthMaxZ = vp.MaxZ;

        // Inverse view rotation and translation
        frameContext.inverseView.m[0][0] = outConstants[4][0]; frameContext.inverseView.m[0][1] = outConstants[4][1]; frameContext.inverseView.m[0][2] = outConstants[4][2]; frameContext.inverseView.m[0][3] = 0.0f;
        frameContext.inverseView.m[1][0] = outConstants[5][0]; frameContext.inverseView.m[1][1] = outConstants[5][1]; frameContext.inverseView.m[1][2] = outConstants[5][2]; frameContext.inverseView.m[1][3] = 0.0f;
        frameContext.inverseView.m[2][0] = outConstants[6][0]; frameContext.inverseView.m[2][1] = outConstants[6][1]; frameContext.inverseView.m[2][2] = outConstants[6][2]; frameContext.inverseView.m[2][3] = 0.0f;
        frameContext.inverseView.m[3][0] = outConstants[7][0]; frameContext.inverseView.m[3][1] = outConstants[7][1]; frameContext.inverseView.m[3][2] = outConstants[7][2]; frameContext.inverseView.m[3][3] = 1.0f;

        // World-space sun direction: transform lightView by inverse view rotation
        Vec3 invX{ outConstants[4][0], outConstants[4][1], outConstants[4][2] };
        Vec3 invY{ outConstants[5][0], outConstants[5][1], outConstants[5][2] };
        Vec3 invZ{ outConstants[6][0], outConstants[6][1], outConstants[6][2] };
        Vec3 lightView{ outConstants[10][0], outConstants[10][1], outConstants[10][2] };
        Vec3 sunWorld = {
            invX.x * lightView.x + invY.x * lightView.y + invZ.x * lightView.z,
            invX.y * lightView.x + invY.y * lightView.y + invZ.y * lightView.z,
            invX.z * lightView.x + invY.z * lightView.y + invZ.z * lightView.z
        };
        float swLen = sqrtf(sunWorld.x * sunWorld.x + sunWorld.y * sunWorld.y + sunWorld.z * sunWorld.z);
        if (swLen > 1e-4f) {
            sunWorld.x /= swLen; sunWorld.y /= swLen; sunWorld.z /= swLen;
        }
        if (sunWorld.z < 0.04f) sunWorld.z = 0.04f;
        swLen = sqrtf(sunWorld.x * sunWorld.x + sunWorld.y * sunWorld.y + sunWorld.z * sunWorld.z);
        sunWorld.x /= swLen; sunWorld.y /= swLen; sunWorld.z /= swLen;
        frameContext.sunDirectionWorld = sunWorld;

        m_valid = true;
        ++m_capturedFrames;
        return true;
    }
}
