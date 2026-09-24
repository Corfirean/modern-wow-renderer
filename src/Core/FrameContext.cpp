#include "FrameContext.h"

namespace renderer
{
    void FrameContext::ResetForNewFrame(uint64_t newFrameIndex, IDirect3DDevice9* dev)
    {
        frameIndex = newFrameIndex;
        device = dev;

        width = 0;
        height = 0;
        viewport = {};

        view.SetIdentity();
        projection.SetIdentity();
        viewProjection.SetIdentity();
        inverseView.SetIdentity();

        cameraPosition = { 0.0f, 0.0f, 0.0f };
        sunDirectionView = { 0.0f, 0.0f, 1.0f };
        sunDirectionWorld = { 0.0f, 0.0f, 1.0f };
        directionalLightColor = { 1.0f, 1.0f, 1.0f };

        daylightFactor = 0.0f;
        moonlightFactor = 0.0f;
        shadowLightFactor = 0.0f;

        sunScreenX = -1.0f;
        sunScreenY = -1.0f;
        sunStrength = 0.0f;

        depthTexture = nullptr;
        depthSurface = nullptr;
        sceneColor = nullptr;
        sceneSurface = nullptr;

        depthAvailable = false;
        cameraValid = false;
        composed = false;
    }
}
