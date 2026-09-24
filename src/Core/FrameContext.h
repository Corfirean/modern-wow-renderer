#pragma once
#include <d3d9.h>
#include <cstdint>
#include "MathTypes.h"

namespace renderer
{
    struct FrameContext
    {
        static FrameContext& Current();

        uint64_t frameIndex = 0;
        IDirect3DDevice9* device = nullptr;

        uint32_t width = 0;
        uint32_t height = 0;
        D3DVIEWPORT9 viewport{};

        Matrix4 view;
        Matrix4 projection;
        Matrix4 viewProjection;
        Matrix4 inverseView;

        Vec3 cameraPosition;

        Vec3 sunDirectionView;
        Vec3 sunDirectionWorld;
        Vec3 directionalLightColor;

        float daylightFactor = 0.0f;
        float moonlightFactor = 0.0f;
        float shadowLightFactor = 0.0f;

        // Coordinates of the sun projected on screen [0..1]
        float sunScreenX = -1.0f;
        float sunScreenY = -1.0f;
        float sunStrength = 0.0f;

        // Projection reconstruction parameters [P22, P32, P00, P11]
        float projUnpack[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float depthMaxZ = 1.0f;

        // Directional shadow map data
        Matrix4 shadowMatrix;
        Matrix4 viewToShadowMatrix;
        IDirect3DTexture9* shadowTexture = nullptr;
        bool shadowMapValid = false;
        uint32_t shadowMapSize = 2048;
        float shadowMapDistance = 180.0f;

        IDirect3DTexture9* depthTexture = nullptr;
        IDirect3DSurface9* depthSurface = nullptr;
        IDirect3DTexture9* sceneColor = nullptr;
        IDirect3DSurface9* sceneSurface = nullptr;

        bool depthAvailable = false;
        bool cameraValid = false;
        bool composed = false;

        void ResetForNewFrame(uint64_t newFrameIndex, IDirect3DDevice9* dev);
    };
}
