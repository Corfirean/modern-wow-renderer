#pragma once
#include <d3d9.h>
#include <cstdint>
#include "MathTypes.h"

namespace renderer
{
    struct FrameContext
    {
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
